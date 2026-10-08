#include "engine/download_task.h"

#include <algorithm>
#include <deque>

#include "core/http_headers.h"
#include "core/resume_state.h"
#include "core/segments.h"
#include "util/file_io.h"
#include "util/unicode.h"
#include "version.h"

namespace dm {
namespace {

using namespace std::chrono_literals;

constexpr size_t kBufferSize = 128 * 1024;
constexpr auto kMonitorInterval = 100ms;
constexpr auto kSpeedWindow = 1500ms;
constexpr int kInitialConnections = 4;  // abertas juntas na largada (a rampa parte daqui)
constexpr auto kRampStep = 1000ms;    // tempo mínimo de cada passo da rampa de conexões
constexpr auto kRampMeasure = 700ms;  // parte final do passo usada para medir (conexões novas já aceleraram)
constexpr double kRampGain = 1.20;    // continua dobrando só se o passo deixou o total 20% mais rápido
constexpr double kRampMinRemainingSeconds = 8.0;  // o que termina antes disso não ganha com conexões novas
// Divide um pedaço em andamento só se a dona levaria mais de 2x isto para terminá-lo (ver monitor()).
constexpr double kSplitHorizonSeconds = 4.0;
constexpr auto kSaveInterval = 3s;

// 0,5 s, 1 s, 2 s, 4 s, 8 s...: a primeira nova tentativa é quase imediata.
std::chrono::milliseconds retryDelay(int failures) {
    return std::chrono::milliseconds(250 << std::clamp(failures, 1, 5));
}

bool isRetryableStatus(int status) {
    return status == 408 || status == 429 || status >= 500;
}

bool isExpiredLinkStatus(int status) {
    return status == 401 || status == 403 || status == 404 || status == 410;
}

DownloadError classifyNetworkError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_INVALID_URL:
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return DownloadError::InvalidUrl;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return DownloadError::NameNotResolved;
        case ERROR_WINHTTP_CANNOT_CONNECT: return DownloadError::CannotConnect;
        case ERROR_WINHTTP_TIMEOUT: return DownloadError::Timeout;
        case ERROR_WINHTTP_CONNECTION_ERROR: return DownloadError::ConnectionLost;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA: return DownloadError::SecureConnection;
        default: return DownloadError::Network;
    }
}

bool isPermanentNetworkError(DWORD code) {
    const DownloadError error = classifyNetworkError(code);
    return error == DownloadError::InvalidUrl || error == DownloadError::SecureConnection;
}

// Recusa típica de proteção contra robôs: o servidor fecha a conexão ou responde 403/406.
bool looksBlocked(int status, DWORD code) {
    return status == 403 || status == 406 || code == ERROR_WINHTTP_INVALID_SERVER_RESPONSE ||
           code == ERROR_WINHTTP_CONNECTION_ERROR;
}

}  // namespace

std::wstring appUserAgent() {
    return L"DownloadManager/" + toWide(DM_VERSION_STRING);
}

std::wstring browserLikeUserAgent() {
    return L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
           L"Chrome/130.0.0.0 Safari/537.36";
}

DownloadTask::DownloadTask(DownloadOptions options) : options_(std::move(options)) {
    options_.connections = std::clamp(options_.connections, 1, 32);
    ownLimiter_.setRate(options_.speedLimit);
}

void DownloadTask::setSpeedLimit(int64_t bytesPerSecond) {
    ownLimiter_.setRate(bytesPerSecond);
}

size_t DownloadTask::readSize(size_t bufferSize) const {
    int64_t limit = ownLimiter_.rate();
    if (options_.sharedLimiter && options_.sharedLimiter->rate() > 0) {
        const int64_t shared = options_.sharedLimiter->rate();
        limit = limit > 0 ? std::min(limit, shared) : shared;
    }
    if (limit <= 0) return bufferSize;
    // Cerca de 1/16 s de dados por leitura, entre 4 KB e o tamanho do buffer.
    return static_cast<size_t>(std::clamp<int64_t>(limit / 16, 4 * 1024, static_cast<int64_t>(bufferSize)));
}

bool DownloadTask::throttle(int64_t bytes) {
    const auto now = RateLimiter::Clock::now();
    auto wait = ownLimiter_.consume(bytes, now);
    if (options_.sharedLimiter) wait = std::max(wait, options_.sharedLimiter->consume(bytes, now));
    if (wait <= std::chrono::nanoseconds::zero()) return !stopRequested_;
    return waitOrStop(std::chrono::duration_cast<std::chrono::milliseconds>(wait) + std::chrono::milliseconds(1));
}

DownloadTask::~DownloadTask() {
    pause();
    wait();
}

void DownloadTask::start() {
    {
        std::lock_guard lock(mutex_);
        if (status_ == DownloadStatus::Connecting || status_ == DownloadStatus::Downloading ||
            status_ == DownloadStatus::Completed) {
            return;
        }
    }
    wait();

    stopRequested_ = false;
    pauseRequested_ = false;
    speed_ = 0;
    {
        std::lock_guard lock(mutex_);
        error_ = DownloadError::None;
        errorDetail_ = 0;
        status_ = DownloadStatus::Connecting;
    }
    thread_ = std::thread(&DownloadTask::run, this);
}

void DownloadTask::pause() {
    pauseRequested_ = true;
    stopAll();
}

void DownloadTask::wait() {
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

bool DownloadTask::setUrl(const std::string& url) {
    std::lock_guard lock(mutex_);
    if (status_ == DownloadStatus::Connecting || status_ == DownloadStatus::Downloading) return false;
    options_.url = url;
    return true;
}

DownloadProgress DownloadTask::progress() const {
    DownloadProgress progress;
    {
        std::lock_guard lock(mutex_);
        progress.status = status_;
        progress.error = error_;
        progress.errorDetail = errorDetail_;
        progress.filePath = targetPath_;
    }
    progress.totalSize = totalSize_;
    progress.downloaded = downloaded_;
    progress.bytesPerSecond = progress.status == DownloadStatus::Downloading ? speed_.load() : 0.0;
    progress.resumable = resumable_;
    {
        std::lock_guard lock(requestsMutex_);
        progress.activeConnections = static_cast<int>(activeRequests_.size());
    }
    return progress;
}

void DownloadTask::run() {
    session_ = std::make_unique<HttpSession>(options_.userAgent.empty() ? appUserAgent() : options_.userAgent);
    {
        std::lock_guard lock(mutex_);
        currentUrl_ = options_.url;
    }

    // Início rápido: tamanho e nome já conhecidos (link direto do Real-Debrid traz os dois). Todas as
    // conexões saem juntas, sem esperar a resposta de uma sondagem: o servidor leva ~0,6 s para responder e
    // ~2 s para engrenar cada pedido, e assim essa espera acontece uma vez só, em paralelo. Cada conexão
    // confere o tamanho na resposta. Retomada (já existe .dmpart) usa o caminho normal, com sondagem.
    if (options_.knownSize > 0 && !options_.fileName.empty() && options_.connections > 1) {
        const std::string name = sanitizeFileName(toUtf8(options_.fileName));
        if (!fileExists(joinPath(options_.directory, toWide(name)) + kPartSuffix)) {
            totalSize_ = options_.knownSize;
            choosePaths(options_.knownSize, {}, {}, name);
            runSegmented(nullptr, false);
            planner_.reset();
            session_.reset();
            return;
        }
    }

    auto request = std::make_unique<HttpRequest>();
    registerRequest(request.get());
    if (!probe(*request)) {
        unregisterRequest(request.get());
        request.reset();
        std::lock_guard lock(mutex_);
        status_ = error_ == DownloadError::None ? DownloadStatus::Paused : DownloadStatus::Failed;
        return;
    }

    const HttpResponse& response = request->response();
    if (options_.rejectWebPages && response.contentType.rfind("text/html", 0) == 0 &&
        response.contentDisposition.find("attachment") == std::string::npos) {
        unregisterRequest(request.get());
        fail(DownloadError::WebPage, 0);
        std::lock_guard lock(mutex_);
        status_ = DownloadStatus::Failed;
        return;
    }
    const bool ranged = response.status == 206 && response.contentRange && response.contentRange->first == 0 &&
                        response.contentRange->total > 0;
    const int64_t total = ranged ? response.contentRange->total : response.contentLength;
    totalSize_ = total;
    // As conexões seguintes vão direto ao endereço final, sem repetir os redirecionamentos.
    currentUrl_ = response.finalUrl;

    std::string name = toUtf8(options_.fileName);
    if (name.empty()) name = fileNameFromContentDisposition(response.contentDisposition);
    if (name.empty()) name = fileNameFromUrl(response.finalUrl);
    if (name.empty()) name = fileNameFromUrl(options_.url);
    name = sanitizeFileName(name);

    {
        std::lock_guard lock(mutex_);
        etag_ = response.etag;
        lastModified_ = response.lastModified;
    }
    const bool resuming = choosePaths(ranged ? total : -1, response.etag, response.lastModified, name);

    if (ranged) {
        runSegmented(std::move(request), resuming);
    } else {
        runSingleStream(std::move(request));
    }
    planner_.reset();
    session_.reset();
}

bool DownloadTask::probe(HttpRequest& request) {
    bool switchedAgent = false;
    for (int attempt = 0;; ++attempt) {
        DWORD code = 0;
        const auto probeStart = std::chrono::steady_clock::now();
        const bool sent = request.send(*session_, currentUrl_, options_.headers, 0, -1, code);
        const int sentStatus = sent ? request.response().status : 0;
        if (GetEnvironmentVariableW(L"DM_DEBUG_SEGMENTS", nullptr, 0) > 0) {
            std::fprintf(stderr, "[sonda] resposta %d em %.3fs\n", sentStatus,
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - probeStart).count());
        }
        // Recusado logo de cara com o User-Agent do app: tenta já com o de navegador (uma vez só).
        if (!switchedAgent && options_.userAgent.empty() && !stopRequested_ && looksBlocked(sentStatus, sent ? 0 : code)) {
            switchedAgent = true;
            request.abort();  // fecha as alças da sessão antiga antes de trocá-la
            session_ = std::make_unique<HttpSession>(browserLikeUserAgent());
            --attempt;
            continue;
        }
        if (sent) {
            const int status = sentStatus;
            if (status == 200 || status == 206) return true;
            if (!isRetryableStatus(status) || attempt >= options_.maxRetries) {
                fail(DownloadError::HttpStatus, static_cast<unsigned long>(status));
                return false;
            }
        } else {
            if (stopRequested_) return false;
            if (isPermanentNetworkError(code) || attempt >= options_.maxRetries) {
                failNetwork(code);
                return false;
            }
        }
        if (!waitOrStop(retryDelay(attempt + 1))) return false;
    }
}

bool DownloadTask::choosePaths(int64_t totalSize, const std::string& etag, const std::string& lastModified,
                               const std::string& suggestedName) {
    const std::wstring baseName = toWide(suggestedName);
    resumeState_.reset();

    for (int number = 0;; ++number) {
        const std::wstring target = joinPath(options_.directory, numberedName(baseName, number));
        const std::wstring part = target + kPartSuffix;
        const std::wstring state = target + kStateSuffix;

        // Um download pela metade deste mesmo arquivo: continua de onde parou.
        if (totalSize > 0 && fileExists(part)) {
            if (const auto text = readTextFile(state)) {
                if (auto saved = parseResumeState(*text); saved && saved->matches(totalSize, etag, lastModified)) {
                    std::lock_guard lock(mutex_);
                    targetPath_ = target;
                    partPath_ = part;
                    statePath_ = state;
                    resumeState_ = std::move(saved);
                    return true;
                }
            }
        }

        if (!fileExists(target) && !fileExists(part)) {
            std::lock_guard lock(mutex_);
            targetPath_ = target;
            partPath_ = part;
            statePath_ = state;
            return false;
        }
    }
}

void DownloadTask::runSegmented(std::unique_ptr<HttpRequest> probeRequest, bool resuming) {
    const int64_t total = totalSize_;
    planner_ = std::make_unique<SegmentPlanner>(total, options_.minSplitSize);

    DWORD code = 0;
    if (!file_.open(partPath_, total, resuming, code)) {
        unregisterRequest(probeRequest.get());
        failFile(code);
        setStatus(DownloadStatus::Failed);
        return;
    }

    std::optional<size_t> firstSegment;
    if (resuming) {
        planner_->restore(resumeState_->segments);
        // A sondagem pediu o arquivo desde o byte 0, que provavelmente já temos: descarta.
        unregisterRequest(probeRequest.get());
        probeRequest.reset();
    } else {
        firstSegment = planner_->acquireFirst();
    }
    downloaded_ = planner_->bytesWritten();
    resumable_ = true;
    saveState();
    setStatus(DownloadStatus::Downloading);

    // Conexões em rampa: começa com uma e o monitor() vai dobrando enquanto a velocidade total sobe.
    std::vector<std::thread> workers;
    runningWorkers_ = 1;
    activeLimit_ = options_.connections;
    workers.emplace_back(&DownloadTask::worker, this, 0, firstSegment, std::move(probeRequest));
    spawnWorker_ = [&] {
        const int index = static_cast<int>(workers.size());
        ++runningWorkers_;
        workers.emplace_back(&DownloadTask::worker, this, index, std::nullopt, nullptr);
    };

    const bool debug = GetEnvironmentVariableW(L"DM_DEBUG_SEGMENTS", nullptr, 0) > 0;
    const auto debugT0 = std::chrono::steady_clock::now();
    auto debugMark = [&](const char* what) {
        if (debug) std::fprintf(stderr, "[fim] %s em +%.3fs\n", what, std::chrono::duration<double>(std::chrono::steady_clock::now() - debugT0).count());
    };
    // Largada: várias conexões de uma vez (cada conexão TCP começa devagar e leva ~1-2 s para acelerar;
    // em paralelo, o total acelera bem mais rápido). Depois a rampa decide se vale abrir mais.
    {
        const int initial = std::min(options_.connections, kInitialConnections);
        for (int i = 1; i < initial; ++i) spawnWorker_();
    }
    monitor();
    debugMark("monitor saiu");
    spawnWorker_ = nullptr;
    for (auto& thread : workers) thread.join();
    debugMark("conexoes encerradas");

    if (planner_->allComplete() && !pauseRequested_) {
        const bool finalized = finalizeFile();
        debugMark("arquivo finalizado");
        if (finalized) {
            setStatus(DownloadStatus::Completed);
            return;
        }
    } else {
        saveState();
        file_.close();
    }

    std::lock_guard lock(mutex_);
    if (error_ == DownloadError::None && !pauseRequested_) error_ = DownloadError::Network;
    status_ = error_ == DownloadError::None ? DownloadStatus::Paused : DownloadStatus::Failed;
}

void DownloadTask::worker(int index, std::optional<size_t> segment, std::unique_ptr<HttpRequest> request) {
    std::vector<char> buffer(kBufferSize);
    int consecutiveFailures = 0;
    bool gotData = false;  // esta conexão já recebeu alguma coisa
    // Conexões adaptativas: servidor que limita conexões por IP recusa as extras (403, 429, 503, página de
    // erro, conexão fechada). Uma conexão extra que nunca funcionou só sai; as outras seguem o download.
    // A última que sobrar decide de verdade (tenta de novo e, se não der, falha com o motivo).
    bool bowedOut = false;
    // Sai do grupo só se ainda sobrar outra conexão (atômico: várias recusadas ao mesmo tempo não zeram o grupo).
    auto leaveIfOthers = [&] {
        int running = runningWorkers_;
        while (running > 1) {
            if (runningWorkers_.compare_exchange_weak(running, running - 1)) {
                bowedOut = true;
                stopSignal_.notify_all();
                return true;
            }
        }
        return false;
    };
    auto canBowOut = [&] { return !gotData && leaveIfOthers(); };
    bool retired = false;
    const bool debug = GetEnvironmentVariableW(L"DM_DEBUG_SEGMENTS", nullptr, 0) > 0;
    auto debugStart = std::chrono::steady_clock::now();
    int64_t debugFrom = 0;
    int64_t debugBytes = 0;

    while (!stopRequested_) {
        if (index >= activeLimit_ && leaveIfOthers()) break;  // excedente antes de pegar outro pedaço
        if (!segment) {
            segment = planner_->acquire();
            if (!segment) break;
        }

        bool failed = false;
        DWORD code = 0;

        if (!request) {
            request = std::make_unique<HttpRequest>();
            registerRequest(request.get());
            const int64_t from = planner_->position(*segment);
            const int64_t to = planner_->end(*segment) - 1;
            debugStart = std::chrono::steady_clock::now();
            debugFrom = from;
            debugBytes = 0;
            if (debug) std::fprintf(stderr, "[%d] pede %lld-%lld (%lld KB)\n", index, (long long)from, (long long)to, (long long)((to - from + 1) / 1024));
            const bool sentOk = request->send(*session_, currentUrl_, options_.headers, from, to, code);
            if (debug) std::fprintf(stderr, "[%d] resposta em %.2fs status %d\n", index, std::chrono::duration<double>(std::chrono::steady_clock::now() - debugStart).count(), sentOk ? request->response().status : -(int)code);
            if (!sentOk) {
                failed = true;
            } else {
                const HttpResponse& response = request->response();
                if (isExpiredLinkStatus(response.status)) {
                    if (canBowOut()) break;
                    fail(DownloadError::LinkExpired, static_cast<unsigned long>(response.status));
                    break;
                }
                if (!etag_.empty() && !response.etag.empty() && response.etag != etag_) {
                    if (canBowOut()) break;  // servidor de erro/limite respondendo outra coisa
                    fail(DownloadError::ServerChanged, 0);
                    break;
                }
                // O servidor precisa devolver exatamente o pedaço pedido.
                failed = response.status != 206 || !response.contentRange || response.contentRange->first != from ||
                         (totalSize_ > 0 && response.contentRange->total != totalSize_);
                if (failed && !isRetryableStatus(response.status) && response.status != 206 &&
                    response.status != 200) {
                    if (canBowOut()) break;
                    fail(DownloadError::HttpStatus, static_cast<unsigned long>(response.status));
                    break;
                }
            }
        }

        while (!failed && !stopRequested_) {
            const int64_t received = request->read(buffer.data(), readSize(buffer.size()), code);
            if (received < 0) {
                failed = true;
                break;
            }
            if (received == 0) {
                // O servidor encerrou antes do fim do pedaço.
                failed = !planner_->reachedEnd(*segment);
                break;
            }

            const auto claim = planner_->reserve(*segment, received);
            if (claim.length > 0) {
                DWORD writeError = 0;
                if (!file_.writeAt(claim.offset, buffer.data(), static_cast<size_t>(claim.length), writeError)) {
                    planner_->release(*segment);
                    segment.reset();
                    failFile(writeError);
                    break;
                }
                planner_->commit(*segment, claim.length);
                debugBytes += claim.length;
                downloaded_ += claim.length;
                consecutiveFailures = 0;
                gotData = true;
            }
            if (!throttle(received)) break;
            // Fim do pedaço (que pode ter encolhido porque outra conexão pegou a metade final).
            if (planner_->reachedEnd(*segment)) break;
            // A rampa viu que conexões demais deixavam mais lento: as excedentes devolvem o pedaço e saem.
            if (index >= activeLimit_ && leaveIfOthers()) {
                retired = true;
                break;
            }
        }
        if (debug) {
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - debugStart).count();
            std::fprintf(stderr, "[%d] fim de %lld: %lld KB em %.2fs = %.1f MB/s%s%s\n", index, (long long)debugFrom, (long long)(debugBytes / 1024), took, took > 0 ? debugBytes / took / 1e6 : 0.0, failed ? " FALHOU" : "", retired ? " SAIU" : "");
        }
        if (retired) break;  // o fim da função devolve o pedaço (com o que já foi baixado) e fecha a conexão

        unregisterRequest(request.get());
        request.reset();
        if (!segment || stopRequested_) break;

        if (!failed) {
            segment.reset();
            continue;
        }

        planner_->release(*segment);
        segment.reset();
        if (canBowOut()) break;
        if (++consecutiveFailures > options_.maxRetries) {
            if (leaveIfOthers()) break;  // as outras conexões continuam
            if (code != 0) {
                failNetwork(code);
            } else {
                fail(DownloadError::ConnectionLost, 0);
            }
            break;
        }
        if (!waitOrStop(retryDelay(consecutiveFailures))) break;
    }

    if (segment) planner_->release(*segment);
    if (request) unregisterRequest(request.get());
    if (!bowedOut) --runningWorkers_;  // quem saiu do grupo já descontou
    stopSignal_.notify_all();
}

void DownloadTask::runSingleStream(std::unique_ptr<HttpRequest> request) {
    // Servidor sem suporte a pedaços: uma conexão só, e pausar recomeça do zero.
    resumable_ = false;
    downloaded_ = 0;

    DWORD code = 0;
    if (!file_.open(partPath_, totalSize_, false, code)) {
        unregisterRequest(request.get());
        failFile(code);
        setStatus(DownloadStatus::Failed);
        return;
    }
    setStatus(DownloadStatus::Downloading);

    runningWorkers_ = 1;
    bool complete = false;
    std::thread reader([&] {
        std::vector<char> buffer(kBufferSize);
        int64_t offset = 0;
        while (!stopRequested_) {
            DWORD readError = 0;
            const int64_t received = request->read(buffer.data(), readSize(buffer.size()), readError);
            if (received < 0) {
                if (!stopRequested_) failNetwork(readError);
                break;
            }
            if (received == 0) {
                complete = totalSize_ < 0 || offset == totalSize_;
                if (!complete) fail(DownloadError::ConnectionLost, 0);
                break;
            }
            DWORD writeError = 0;
            if (!file_.writeAt(offset, buffer.data(), static_cast<size_t>(received), writeError)) {
                failFile(writeError);
                break;
            }
            offset += received;
            downloaded_ = offset;
            if (!throttle(received)) break;
        }
        unregisterRequest(request.get());
        --runningWorkers_;
        stopSignal_.notify_all();
    });

    monitor();
    reader.join();

    if (complete && finalizeFile()) {
        totalSize_ = downloaded_.load();
        setStatus(DownloadStatus::Completed);
        return;
    }
    file_.close();
    DeleteFileW(partPath_.c_str());
    std::lock_guard lock(mutex_);
    status_ = error_ == DownloadError::None ? DownloadStatus::Paused : DownloadStatus::Failed;
}

void DownloadTask::monitor() {
    using Clock = std::chrono::steady_clock;
    // Velocidade "ao vivo": amostras a cada 100 ms e média do último 1,5 s (janela deslizante). Atualiza
    // 10 vezes por segundo sem pular, e reage logo quando a velocidade muda de verdade.
    struct Sample {
        Clock::time_point time;
        int64_t bytes;
    };
    std::deque<Sample> window{{Clock::now(), downloaded_.load()}};
    auto lastSave = window.front().time;
    const bool debugProfile = GetEnvironmentVariableW(L"DM_DEBUG_SEGMENTS", nullptr, 0) > 0;
    const auto debugBegin = window.front().time;
    auto debugLast = debugBegin;
    int64_t debugLastBytes = downloaded_;

    // Rampa de conexões: 1 -> 2 -> 4 -> 8 -> 16 enquanto cada passo deixar o total pelo menos 20% mais
    // rápido. Servidor que limita por conexão ganha todas; servidor que limita por usuário/IP (ou demora a
    // responder cada conexão nova, como o Real-Debrid) fica com poucas. Se o último passo piorou, volta.
    bool ramping = static_cast<bool>(spawnWorker_) && options_.connections > 1;
    int spawned = std::max(1, runningWorkers_.load());  // a largada já abriu algumas
    int bestCount = 1;
    double bestSpeed = 0;
    double previousMeasure = 0;
    auto stepStart = window.front().time;
    auto lastRampCheck = stepStart;
    // Velocidade só dos últimos instantes do passo (as conexões novas já aceleraram).
    auto recentSpeed = [&](Clock::time_point now) {
        const auto from = now - kRampMeasure;
        auto first = window.begin();
        while (std::next(first) != window.end() && std::next(first)->time <= from) ++first;
        const double seconds = std::chrono::duration<double>(window.back().time - first->time).count();
        return seconds > 0.1 ? static_cast<double>(window.back().bytes - first->bytes) / seconds : 0.0;
    };

    std::unique_lock lock(stopMutex_);
    while (runningWorkers_ > 0) {
        stopSignal_.wait_for(lock, kMonitorInterval, [&] { return runningWorkers_ == 0; });

        const auto now = Clock::now();
        window.push_back({now, downloaded_.load()});
        while (window.size() > 2 && now - window[1].time >= kSpeedWindow) window.pop_front();
        const double seconds = std::chrono::duration<double>(now - window.front().time).count();
        if (seconds >= 0.25) {
            speed_ = static_cast<double>(window.back().bytes - window.front().bytes) / seconds;
        }
        if (debugProfile && now - debugLast >= std::chrono::milliseconds(500)) {
            const int64_t bytes = downloaded_;
            std::fprintf(stderr, "[perfil] %.1fs %.1f MB %.1f MB/s %d conexoes\n",
                         std::chrono::duration<double>(now - debugBegin).count(), static_cast<double>(bytes) / 1e6,
                         static_cast<double>(bytes - debugLastBytes) / 1e6 /
                             std::chrono::duration<double>(now - debugLast).count(),
                         runningWorkers_.load());
            debugLast = now;
            debugLastBytes = bytes;
        }
        // Dividir um pedaço só compensa se a conexão dona ainda levaria mais de ~8 s para terminá-lo:
        // cada conexão nova paga o tempo de resposta do servidor e começa devagar (no Real-Debrid, ~0,6 s +
        // ~2 s acelerando). Sem isso, o fim do download virava uma fila de pedacinhos lentos.
        if (planner_ && speed_ > 0) {
            size_t active = 1;
            {
                std::lock_guard requests(requestsMutex_);
                active = std::max<size_t>(activeRequests_.size(), 1);
            }
            const double perConnection = speed_ / static_cast<double>(active);
            planner_->setMinSplit(static_cast<int64_t>(perConnection * kSplitHorizonSeconds));
        }

        if (ramping && !stopRequested_ && now - stepStart >= kRampStep && now - lastRampCheck >= kRampMeasure) {
            lastRampCheck = now;
            const double current = recentSpeed(now);
            const int64_t remaining = totalSize_ - downloaded_;
            // As conexões ainda estão acelerando (TCP começa devagar)? Espera estabilizar antes de decidir,
            // senão a medida engana (até ~5 s por passo).
            // A primeira medida do passo só serve de referência.
            const bool accelerating = previousMeasure <= 0 || current > previousMeasure * 1.15;
            previousMeasure = current;
            if ((current <= 0 || accelerating) && now - stepStart < kRampStep * 4) {
                // espera
            } else if (current > 0 && static_cast<double>(remaining) / current < kRampMinRemainingSeconds) {
                ramping = false;  // termina logo com as conexões que já tem
            } else if (bestSpeed <= 0 || current > bestSpeed * kRampGain) {
                // Quase dobrou no último passo (servidor limita por conexão): o próximo passo quadruplica.
                const int factor = bestSpeed > 0 && current > bestSpeed * 1.7 ? 4 : 2;
                if (current > bestSpeed) {
                    bestSpeed = current;
                    bestCount = spawned;
                }
                const int target = std::min(options_.connections, spawned * factor);
                lock.unlock();
                for (; spawned < target && !stopRequested_; ++spawned) spawnWorker_();
                lock.lock();
                if (spawned >= options_.connections) ramping = false;
                stepStart = now;
                previousMeasure = 0;
            } else {
                // Mais conexões não ajudaram. Se ficou bem pior, volta para a melhor quantidade medida.
                if (current < bestSpeed * 0.85) activeLimit_ = bestCount;
                ramping = false;
            }
        }
        if (now - lastSave >= kSaveInterval && planner_) {
            lock.unlock();
            saveState();
            lock.lock();
            lastSave = now;
        }
    }
    speed_ = 0;
}

void DownloadTask::saveState() {
    if (!planner_) return;
    ResumeState state;
    {
        std::lock_guard lock(mutex_);
        state.url = options_.url;
        state.fileName = toUtf8(fileNameOf(targetPath_));
        state.etag = etag_;
        state.lastModified = lastModified_;
    }
    state.totalSize = planner_->totalSize();
    state.segments = planner_->snapshot();
    // Os bytes contados no estado precisam estar no disco antes do estado.
    file_.flush();
    writeTextFileAtomically(statePath_, serializeResumeState(state));
}

bool DownloadTask::finalizeFile() {
    file_.finish();
    DeleteFileW(statePath_.c_str());

    std::wstring target = targetPath_;
    if (fileExists(target)) {
        // Alguém criou um arquivo com o mesmo nome enquanto baixávamos: não sobrescreve.
        const std::wstring directory = directoryOf(target);
        const std::wstring name = fileNameOf(target);
        for (int number = 1; fileExists(target); ++number) target = joinPath(directory, numberedName(name, number));
    }
    if (!MoveFileExW(partPath_.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
        failFile(GetLastError());
        return false;
    }
    std::lock_guard lock(mutex_);
    targetPath_ = target;
    return true;
}

void DownloadTask::registerRequest(HttpRequest* request) {
    if (!request) return;
    std::lock_guard lock(requestsMutex_);
    activeRequests_.push_back(request);
    if (stopRequested_) request->abort();
}

void DownloadTask::unregisterRequest(HttpRequest* request) {
    if (!request) return;
    std::lock_guard lock(requestsMutex_);
    std::erase(activeRequests_, request);
}

void DownloadTask::stopAll() {
    {
        std::lock_guard lock(stopMutex_);
        stopRequested_ = true;
    }
    stopSignal_.notify_all();
    std::lock_guard lock(requestsMutex_);
    for (HttpRequest* request : activeRequests_) request->abort();
}

void DownloadTask::fail(DownloadError error, unsigned long detail) {
    {
        std::lock_guard lock(mutex_);
        if (error_ == DownloadError::None) {
            error_ = error;
            errorDetail_ = detail;
        }
    }
    stopAll();
}

void DownloadTask::failNetwork(DWORD code) {
    fail(classifyNetworkError(code), code);
}

void DownloadTask::failFile(DWORD code) {
    switch (code) {
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: fail(DownloadError::DiskFull, code); break;
        case ERROR_ACCESS_DENIED: fail(DownloadError::AccessDenied, code); break;
        default: fail(DownloadError::FileSystem, code); break;
    }
}

bool DownloadTask::waitOrStop(std::chrono::milliseconds duration) {
    std::unique_lock lock(stopMutex_);
    return !stopSignal_.wait_for(lock, duration, [&] { return stopRequested_.load(); });
}

void DownloadTask::setStatus(DownloadStatus status) {
    std::lock_guard lock(mutex_);
    status_ = status;
}

}  // namespace dm
