#include "app/download_manager.h"

#include <windows.h>

#include <algorithm>

#include "app/system.h"
#include "app/video_tools.h"
#include "core/http_headers.h"
#include "engine/debrid_task.h"
#include "engine/video_task.h"
#include "core/rate_limiter.h"
#include "util/file_io.h"
#include "util/secure.h"
#include "util/unicode.h"

namespace app {
namespace {

constexpr int kSaveEveryTicks = 10;  // com o timer de 500 ms: salva o progresso a cada 5 s

// Cabeçalhos guardados como linhas "Nome: valor", criptografadas para o usuário atual.
std::string protectHeaders(const std::vector<std::pair<std::string, std::string>>& headers) {
    std::string lines;
    for (const auto& [name, value] : headers) lines += name + ": " + value + "\n";
    return dm::protectForCurrentUser(lines);
}

std::vector<dm::HttpHeader> unprotectHeaders(const std::string& stored) {
    std::vector<dm::HttpHeader> headers;
    const auto lines = dm::unprotectForCurrentUser(stored);
    if (!lines) return headers;
    size_t start = 0;
    while (start < lines->size()) {
        size_t end = lines->find('\n', start);
        if (end == std::string::npos) end = lines->size();
        const std::string line = lines->substr(start, end - start);
        if (const size_t colon = line.find(": "); colon != std::string::npos) {
            headers.push_back({line.substr(0, colon), line.substr(colon + 2)});
        }
        start = end + 1;
    }
    return headers;
}

}  // namespace

DownloadManager::DownloadManager(std::wstring listPath) : listPath_(std::move(listPath)) {}

DownloadManager::~DownloadManager() {
    shutdown();
}

void DownloadManager::load() {
    const auto text = dm::readTextFile(listPath_);
    if (!text) return;
    for (auto& record : dm::parseDownloadList(*text)) {
        nextId_ = std::max(nextId_, record.id + 1);
        auto item = std::make_unique<DownloadItem>();
        item->record = std::move(record);
        items_.push_back(std::move(item));
    }
    // O que estava baixando ou na fila volta para a fila; advanceQueue() começa respeitando as vagas.
    for (auto& item : items_) {
        if (item->record.state == dm::RecordState::Active) item->record.state = dm::RecordState::Queued;
    }
    advanceQueue();
}

void DownloadManager::setRules(std::vector<dm::Rule> rules, bool enabled, std::wstring baseFolder) {
    rules_ = std::move(rules);
    rulesEnabled_ = enabled;
    baseFolder_ = std::move(baseFolder);
}

void DownloadManager::setOrganize(uint64_t id, bool organize) {
    if (DownloadItem* item = find(id)) {
        item->record.organize = organize;
        save();
    }
}

void DownloadManager::organizeIfNeeded(DownloadItem& item) {
    if (!item.record.organize || !rulesEnabled_ || item.record.filePath.empty()) return;
    item.record.organize = false;  // uma vez só
    const std::wstring path = dm::toWide(item.record.filePath);
    const dm::DownloadFacts facts{item.record.url, dm::toUtf8(dm::fileNameOf(path)), item.record.totalSize,
                                  item.record.isVideo};
    const dm::Rule* rule = dm::matchRule(rules_, facts);
    if (!rule) return;
    item.organizing = true;
    organizer_.submit({item.record.id, path, baseFolder_, *rule});
}

bool DownloadManager::collectOrganized() {
    bool changed = false;
    for (const auto& result : organizer_.takeResults()) {
        DownloadItem* item = find(result.id);
        if (!item) continue;
        item->organizing = false;
        item->record.filePath = dm::toUtf8(result.path);
        item->record.directory = dm::toUtf8(dm::directoryOf(result.path));
        changed = true;
        if (onOrganized && (result.openFile || result.openFolder)) {
            onOrganized(result.path, result.openFile, result.openFolder);
        }
    }
    return changed;
}

void DownloadManager::setMaxRunning(int count) {
    maxRunning_ = std::max(count, 1);
}

void DownloadManager::setSchedule(bool enabled, int startMinute, int endMinute) {
    scheduleEnabled_ = enabled;
    scheduleStart_ = startMinute;
    scheduleEnd_ = endMinute;
}

void DownloadManager::setGlobalSpeedLimit(int64_t bytesPerSecond) {
    globalLimiter_->setRate(bytesPerSecond);
}

int DownloadManager::runningCount() const {
    int count = 0;
    for (const auto& item : items_) count += item->running() ? 1 : 0;
    return count;
}

bool DownloadManager::idle() const {
    for (const auto& item : items_) {
        if (item->running() || item->queued()) return false;
    }
    return true;
}

bool DownloadManager::scheduleOpen() const {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return dm::scheduleAllows(scheduleEnabled_, scheduleStart_, scheduleEnd_, now.wHour * 60 + now.wMinute);
}

void DownloadManager::enqueue(DownloadItem& item) {
    stopTask(item);
    item.record.state = dm::RecordState::Queued;
    item.record.errorCode = 0;
}

bool DownloadManager::advanceQueue() {
    bool changed = false;
    const bool open = scheduleOpen();

    if (!open) {
        // Fora do horário: o que não foi forçado volta para a fila.
        for (auto& item : items_) {
            if (item->running() && !item->forced && !item->pausedBySchedule) {
                item->pausedBySchedule = true;
                item->task->pause();
                changed = true;
            }
        }
        return changed;
    }

    int running = runningCount();
    const bool videoReady = videoTools_ && videoTools_->ready();
    for (auto& item : items_) {
        if (running >= maxRunning_) break;
        if (!item->queued() || (item->record.isVideo && !videoReady)) continue;
        startTask(*item);
        ++running;
        changed = true;
    }
    return changed;
}

void DownloadManager::save() {
    std::vector<dm::DownloadRecord> records;
    records.reserve(items_.size());
    for (const auto& item : items_) records.push_back(item->record);
    dm::writeTextFileAtomically(listPath_, dm::serializeDownloadList(records));
    ticksSinceSave_ = 0;
}

uint64_t DownloadManager::add(const std::string& url, const std::wstring& directory, const std::wstring& fileName,
                              int connections, const std::vector<std::pair<std::string, std::string>>& headers,
                              const std::string& userAgent, bool rejectWebPages) {
    auto item = std::make_unique<DownloadItem>();
    item->record.id = nextId_++;
    item->record.url = url;
    item->record.directory = dm::toUtf8(directory);
    item->record.fileName = dm::toUtf8(fileName);
    item->record.connections = connections;
    item->record.addedAt = unixNow();
    item->record.protectedHeaders = protectHeaders(headers);
    item->record.userAgent = userAgent;
    item->rejectWebPages = rejectWebPages;
    const uint64_t id = item->record.id;
    item->record.state = dm::RecordState::Queued;
    items_.push_back(std::move(item));
    advanceQueue();
    save();
    return id;
}

uint64_t DownloadManager::addVideo(const std::string& url, const std::wstring& directory, const std::wstring& title,
                                   const dm::VideoFormat& format, bool subtitles,
                                   const std::vector<std::pair<std::string, std::string>>& headers,
                                   const std::string& userAgent) {
    auto item = std::make_unique<DownloadItem>();
    item->record.id = nextId_++;
    item->record.url = url;
    item->record.directory = dm::toUtf8(directory);
    item->record.fileName = title.empty() ? std::string{} : dm::sanitizeFileName(dm::toUtf8(title));
    item->record.isVideo = true;
    item->record.videoFormat = format.serialize();
    item->record.subtitles = subtitles;
    item->record.addedAt = unixNow();
    item->record.protectedHeaders = protectHeaders(headers);
    item->record.userAgent = userAgent;
    item->record.state = dm::RecordState::Queued;
    const uint64_t id = item->record.id;
    items_.push_back(std::move(item));
    advanceQueue();
    save();
    return id;
}

uint64_t DownloadManager::addTorrent(const std::string& source, const std::wstring& directory,
                                     const std::wstring& displayName, int connections) {
    auto item = std::make_unique<DownloadItem>();
    item->record.id = nextId_++;
    item->record.url = source;
    item->record.debrid = true;
    item->record.directory = dm::toUtf8(directory);
    item->record.fileName = dm::toUtf8(displayName);  // só para a lista; os nomes finais vêm do Real-Debrid
    item->record.connections = connections;
    item->record.addedAt = unixNow();
    item->record.state = dm::RecordState::Queued;
    const uint64_t id = item->record.id;
    items_.push_back(std::move(item));
    advanceQueue();
    save();
    return id;
}

void DownloadManager::deleteTorrentCopy(const dm::DownloadRecord& record) {
    if (!record.debrid || dm::isMagnetLink(record.url)) return;
    // Só apaga a cópia que o app fez (dentro da pasta de dados), nunca o arquivo original do usuário.
    const std::wstring path = dm::toWide(record.url);
    const std::wstring folder = dm::joinPath(dm::directoryOf(listPath_), L"torrents");
    if (_wcsnicmp(path.c_str(), folder.c_str(), folder.size()) == 0) DeleteFileW(path.c_str());
}

void DownloadManager::finishDebrid(DownloadItem& item, const std::vector<dm::DebridLink>& links) {
    dm::DownloadRecord& record = item.record;
    if (links.empty()) {
        record.state = dm::RecordState::Failed;
        record.errorCode = static_cast<int>(dm::DownloadError::Debrid);
        record.errorDetail = static_cast<unsigned long>(dm::DebridError::TorrentFailed);
        return;
    }
    deleteTorrentCopy(record);
    const dm::DownloadRecord base = record;
    auto makeDirect = [&](dm::DownloadRecord& target, const dm::DebridLink& link) {
        target.debrid = false;
        target.debridId.clear();
        target.url = link.download;
        target.fileName = dm::sanitizeFileName(link.fileName);
        target.filePath.clear();
        target.totalSize = link.size;
        target.downloaded = 0;
        target.errorCode = 0;
        target.state = dm::RecordState::Queued;
    };
    makeDirect(record, links.front());
    for (size_t i = 1; i < links.size(); ++i) {
        auto extra = std::make_unique<DownloadItem>();
        extra->record = base;
        extra->record.id = nextId_++;
        makeDirect(extra->record, links[i]);
        items_.push_back(std::move(extra));
    }
}

uint64_t DownloadManager::addCompleted(const std::string& url, const std::wstring& filePath, int64_t size) {
    auto item = std::make_unique<DownloadItem>();
    item->record.id = nextId_++;
    item->record.url = url;
    item->record.directory = dm::toUtf8(dm::directoryOf(filePath));
    item->record.filePath = dm::toUtf8(filePath);
    item->record.state = dm::RecordState::Completed;
    item->record.totalSize = size;
    item->record.downloaded = size;
    item->record.addedAt = unixNow();
    item->record.finishedAt = item->record.addedAt;
    // Não é movido: o navegador guarda o caminho e quebraria (no Chrome/Edge já veio na pasta da regra).
    item->record.organize = false;
    const uint64_t id = item->record.id;
    items_.push_back(std::move(item));
    save();
    return id;
}

void DownloadManager::resume(uint64_t id) {
    DownloadItem* item = find(id);
    if (!item || item->completed() || item->running()) return;
    enqueue(*item);
    advanceQueue();
    save();
}

void DownloadManager::startNow(uint64_t id) {
    DownloadItem* item = find(id);
    if (!item || item->completed() || item->running()) return;
    startTask(*item);
    item->forced = true;
    save();
}

void DownloadManager::pause(uint64_t id) {
    DownloadItem* item = find(id);
    if (!item) return;
    if (item->task) {
        item->task->pause();
    } else if (item->queued()) {
        item->record.state = dm::RecordState::Paused;
        save();
    }
}

void DownloadManager::setSpeedLimit(uint64_t id, int64_t bytesPerSecond) {
    DownloadItem* item = find(id);
    if (!item) return;
    item->record.speedLimit = std::max<int64_t>(bytesPerSecond, 0);
    if (item->task) item->task->setSpeedLimit(item->record.speedLimit);
    save();
}

void DownloadManager::remove(uint64_t id, bool deleteFiles) {
    const auto it = std::find_if(items_.begin(), items_.end(),
                                 [id](const auto& item) { return item->record.id == id; });
    if (it == items_.end()) return;
    DownloadItem& item = **it;
    stopTask(item);
    if (deleteFiles && !item.completed() && !item.record.filePath.empty()) {
        const std::wstring path = dm::toWide(item.record.filePath);
        DeleteFileW((path + dm::kPartSuffix).c_str());
        DeleteFileW((path + dm::kStateSuffix).c_str());
    }
    deleteTorrentCopy(item.record);
    items_.erase(it);
    save();
}

bool DownloadManager::changeUrl(uint64_t id, const std::string& url) {
    DownloadItem* item = find(id);
    if (!item || item->completed() || item->running()) return false;
    stopTask(*item);
    item->record.url = url;
    enqueue(*item);
    advanceQueue();
    save();
    return true;
}

void DownloadManager::shutdown() {
    for (auto& item : items_) {
        if (item->running() || item->pausedBySchedule) item->record.state = dm::RecordState::Active;
        if (item->task) item->task->pause();
    }
    for (auto& item : items_) {
        if (!item->task) continue;
        const bool wasActive = item->record.state == dm::RecordState::Active;
        stopTask(*item);
        // Volta na próxima vez (como fila: respeita vagas e horário).
        if (wasActive && !item->completed()) item->record.state = dm::RecordState::Queued;
    }
    // Arquivos sendo movidos/extraídos: espera para gravar o caminho final.
    organizer_.drain();
    collectOrganized();
    if (!items_.empty()) save();
}

bool DownloadManager::tick() {
    bool changed = false;
    // Links que eram páginas web: saem da lista e viram análise de vídeo (fora do laço, que não pode remover).
    std::vector<dm::DownloadRecord> webPages;
    // Torrents prontos no Real-Debrid: viram downloads diretos depois do laço (que não pode criar itens).
    std::vector<std::pair<uint64_t, std::vector<dm::DebridLink>>> debridDone;
    for (auto& item : items_) {
        if (!item->task) continue;
        const dm::DownloadStatus previous = item->live.status;
        item->live = item->task->progress();
        dm::DownloadRecord& record = item->record;
        if (item->live.totalSize >= 0) record.totalSize = item->live.totalSize;
        if (item->live.status != dm::DownloadStatus::Connecting) record.downloaded = item->live.downloaded;
        if (!item->live.filePath.empty()) record.filePath = dm::toUtf8(item->live.filePath);
        if (record.debrid) {
            if (!item->live.remoteId.empty() && item->live.remoteId != record.debridId) {
                record.debridId = item->live.remoteId;
                changed = true;  // salva logo: não mandar o mesmo torrent duas vezes
            }
            if (!item->live.remoteName.empty()) record.fileName = item->live.remoteName;
        }

        if (item->live.status == previous) continue;
        changed = true;
        switch (item->live.status) {
            case dm::DownloadStatus::Completed:
                if (record.debrid) {
                    const auto* debrid = dynamic_cast<const dm::DebridTask*>(item->task.get());
                    debridDone.emplace_back(record.id, debrid ? debrid->links() : std::vector<dm::DebridLink>{});
                    stopTask(*item);
                    break;
                }
                stopTask(*item);
                record.state = dm::RecordState::Completed;
                record.finishedAt = unixNow();
                record.errorCode = 0;
                organizeIfNeeded(*item);
                if (onCompleted) onCompleted(*item);
                break;
            case dm::DownloadStatus::Failed:
                if (item->live.error == dm::DownloadError::WebPage) webPages.push_back(record);
                record.state = dm::RecordState::Failed;
                record.errorCode = static_cast<int>(item->live.error);
                record.errorDetail = item->live.errorDetail;
                record.errorText = item->live.errorText;
                break;
            case dm::DownloadStatus::Paused:
                if (item->pausedBySchedule) {
                    item->pausedBySchedule = false;
                    enqueue(*item);
                } else {
                    record.state = dm::RecordState::Paused;
                }
                item->forced = false;
                break;
            default:
                record.state = dm::RecordState::Active;
                record.errorCode = 0;
                break;
        }
    }
    for (const auto& record : webPages) {
        remove(record.id, true);
        if (onWebPage) onWebPage(record);
    }
    for (const auto& [id, links] : debridDone) {
        if (DownloadItem* item = find(id)) finishDebrid(*item, links);
    }
    if (collectOrganized()) changed = true;
    if (advanceQueue()) changed = true;
    if (changed || ++ticksSinceSave_ >= kSaveEveryTicks) save();
    return changed;
}

DownloadItem* DownloadManager::find(uint64_t id) {
    for (auto& item : items_) {
        if (item->record.id == id) return item.get();
    }
    return nullptr;
}

namespace {

// No modelo de nome do yt-dlp, "%" tem significado: um título com "100%" vira "100%%".
std::string escapeTemplate(const std::string& text) {
    std::string escaped;
    for (const char c : text) {
        escaped += c;
        if (c == '%') escaped += '%';
    }
    return escaped;
}

}  // namespace

void DownloadManager::startTask(DownloadItem& item) {
    const bool rejectWebPages = item.rejectWebPages;
    stopTask(item);
    if (item.record.debrid) {
        dm::DebridTaskOptions options;
        options.token = debridToken_;
        if (dm::isMagnetLink(item.record.url)) {
            options.magnet = item.record.url;
        } else {
            options.torrentFile = dm::toWide(item.record.url);
        }
        options.torrentId = item.record.debridId;
        item.task = std::make_unique<dm::DebridTask>(std::move(options));
        item.task->start();
        item.live = item.task->progress();
        item.record.state = dm::RecordState::Active;
        item.record.errorCode = 0;
        return;
    }
    if (item.record.isVideo) {
        if (!videoTools_ || !videoTools_->ready()) {
            item.record.state = dm::RecordState::Queued;
            return;
        }
        dm::VideoTaskOptions options;
        options.ytDlpPath = videoTools_->ytDlpPath();
        options.tempDirectory = videoTools_->tempDirectory();
        options.job.url = item.record.url;
        options.job.outputDirectory = item.record.directory;
        options.job.fileNameTemplate = escapeTemplate(item.record.fileName);
        options.job.format = dm::VideoFormat::parse(item.record.videoFormat);
        options.job.subtitles = item.record.subtitles;
        options.job.ffmpegDirectory = dm::toUtf8(videoTools_->ffmpegDirectory());
        options.job.userAgent = item.record.userAgent;
        options.job.speedLimit = item.record.speedLimit;
        for (const auto& header : unprotectHeaders(item.record.protectedHeaders)) {
            if (header.name == "Cookie") options.cookieHeader = header.value;
            if (header.name == "Referer") options.job.referrer = header.value;
        }
        item.task = std::make_unique<dm::VideoTask>(std::move(options));
        item.task->start();
        item.live = item.task->progress();
        item.record.state = dm::RecordState::Active;
        item.record.errorCode = 0;
        item.record.errorText.clear();
        return;
    }
    dm::DownloadOptions options;
    options.url = item.record.url;
    options.connections = item.record.connections;
    options.speedLimit = item.record.speedLimit;
    options.sharedLimiter = globalLimiter_;
    options.headers = unprotectHeaders(item.record.protectedHeaders);
    options.rejectWebPages = rejectWebPages;
    if (!item.record.userAgent.empty()) options.userAgent = dm::toWide(item.record.userAgent);
    if (!item.record.filePath.empty()) {
        // Já sabemos o arquivo: mira nele, para continuar de onde parou.
        const std::wstring path = dm::toWide(item.record.filePath);
        options.directory = dm::directoryOf(path);
        options.fileName = dm::fileNameOf(path);
    } else {
        options.directory = dm::toWide(item.record.directory);
        options.fileName = dm::toWide(item.record.fileName);
    }
    item.task = std::make_unique<dm::DownloadTask>(std::move(options));
    item.task->start();
    item.live = item.task->progress();
    item.record.state = dm::RecordState::Active;
    item.record.errorCode = 0;
}

void DownloadManager::stopTask(DownloadItem& item) {
    if (!item.task) return;
    item.task->pause();
    item.task->wait();
    const dm::DownloadProgress last = item.task->progress();
    if (last.totalSize >= 0) item.record.totalSize = last.totalSize;
    if (!last.filePath.empty()) item.record.filePath = dm::toUtf8(last.filePath);
    item.record.downloaded = last.status == dm::DownloadStatus::Completed || last.resumable ? last.downloaded : 0;
    if (item.record.state == dm::RecordState::Active && last.status != dm::DownloadStatus::Completed) {
        item.record.state = last.status == dm::DownloadStatus::Failed ? dm::RecordState::Failed
                                                                      : dm::RecordState::Paused;
    }
    item.task.reset();
    item.live = {};
    item.forced = false;
    item.pausedBySchedule = false;
    item.rejectWebPages = false;  // só na primeira tentativa
}

}  // namespace app
