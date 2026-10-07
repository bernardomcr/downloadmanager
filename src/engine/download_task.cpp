#include "engine/download_task.h"

#include <algorithm>

#include "core/http_headers.h"
#include "core/resume_state.h"
#include "core/segments.h"
#include "util/unicode.h"

namespace dm {
namespace {

using namespace std::chrono_literals;

constexpr size_t kBufferSize = 128 * 1024;
constexpr auto kMonitorInterval = 500ms;
constexpr auto kSaveInterval = 3s;

bool fileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::optional<std::string> readTextFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) text.append(buffer, read);
    CloseHandle(file);
    return text;
}

// Grava num .tmp e troca de uma vez: um travamento no meio nunca deixa o estado corrompido.
bool writeTextFileAtomically(const std::wstring& path, const std::string& text) {
    const std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

// "video.mp4", 2 -> "video (2).mp4"
std::wstring numberedName(const std::wstring& name, int number) {
    if (number == 0) return name;
    const size_t dot = name.find_last_of(L'.');
    const std::wstring suffix = L" (" + std::to_wstring(number) + L")";
    if (dot == std::wstring::npos || dot == 0) return name + suffix;
    return name.substr(0, dot) + suffix + name.substr(dot);
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) return name;
    const wchar_t last = directory.back();
    return (last == L'\\' || last == L'/') ? directory + name : directory + L'\\' + name;
}

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

}  // namespace

DownloadTask::DownloadTask(DownloadOptions options) : options_(std::move(options)) {
    options_.connections = std::clamp(options_.connections, 1, 32);
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
    session_ = std::make_unique<HttpSession>(options_.userAgent);
    {
        std::lock_guard lock(mutex_);
        currentUrl_ = options_.url;
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
    for (int attempt = 0;; ++attempt) {
        DWORD code = 0;
        if (request.send(*session_, currentUrl_, options_.headers, 0, -1, code)) {
            const int status = request.response().status;
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
        const std::wstring part = target + L".dmpart";
        const std::wstring state = target + L".dmstate";

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

    std::vector<std::thread> workers;
    runningWorkers_ = options_.connections;
    workers.emplace_back(&DownloadTask::worker, this, firstSegment, std::move(probeRequest));
    for (int i = 1; i < options_.connections; ++i) {
        workers.emplace_back(&DownloadTask::worker, this, std::nullopt, nullptr);
    }

    monitor();
    for (auto& thread : workers) thread.join();

    if (planner_->allComplete() && !pauseRequested_) {
        if (finalizeFile()) {
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

void DownloadTask::worker(std::optional<size_t> segment, std::unique_ptr<HttpRequest> request) {
    std::vector<char> buffer(kBufferSize);
    int consecutiveFailures = 0;

    while (!stopRequested_) {
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
            if (!request->send(*session_, currentUrl_, options_.headers, from, to, code)) {
                failed = true;
            } else {
                const HttpResponse& response = request->response();
                if (isExpiredLinkStatus(response.status)) {
                    fail(DownloadError::LinkExpired, static_cast<unsigned long>(response.status));
                    break;
                }
                if (!etag_.empty() && !response.etag.empty() && response.etag != etag_) {
                    fail(DownloadError::ServerChanged, 0);
                    break;
                }
                // O servidor precisa devolver exatamente o pedaço pedido.
                failed = response.status != 206 || !response.contentRange || response.contentRange->first != from;
                if (failed && !isRetryableStatus(response.status) && response.status != 206 &&
                    response.status != 200) {
                    fail(DownloadError::HttpStatus, static_cast<unsigned long>(response.status));
                    break;
                }
            }
        }

        while (!failed && !stopRequested_) {
            const int64_t received = request->read(buffer.data(), buffer.size(), code);
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
                downloaded_ += claim.length;
                consecutiveFailures = 0;
            }
            // Fim do pedaço (que pode ter encolhido porque outra conexão pegou a metade final).
            if (planner_->reachedEnd(*segment)) break;
        }

        unregisterRequest(request.get());
        request.reset();
        if (!segment || stopRequested_) break;

        if (!failed) {
            segment.reset();
            continue;
        }

        planner_->release(*segment);
        segment.reset();
        if (++consecutiveFailures > options_.maxRetries) {
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
    --runningWorkers_;
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
            const int64_t received = request->read(buffer.data(), buffer.size(), readError);
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
    auto lastSample = std::chrono::steady_clock::now();
    auto lastSave = lastSample;
    int64_t lastBytes = downloaded_;

    std::unique_lock lock(stopMutex_);
    while (runningWorkers_ > 0) {
        stopSignal_.wait_for(lock, kMonitorInterval, [&] { return runningWorkers_ == 0; });

        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - lastSample).count();
        if (seconds > 0.2) {
            const int64_t bytes = downloaded_;
            const double instant = static_cast<double>(bytes - lastBytes) / seconds;
            const double previous = speed_;
            speed_ = previous == 0 ? instant : previous * 0.7 + instant * 0.3;
            lastSample = now;
            lastBytes = bytes;
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
        state.fileName = toUtf8(targetPath_.substr(targetPath_.find_last_of(L"\\/") + 1));
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
        const std::wstring directory = target.substr(0, target.find_last_of(L"\\/") + 1);
        const std::wstring name = target.substr(directory.size());
        for (int number = 1; fileExists(target); ++number) target = directory + numberedName(name, number);
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
