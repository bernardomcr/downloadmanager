#include "app/download_manager.h"

#include <windows.h>

#include <algorithm>

#include "app/system.h"
#include "core/rate_limiter.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

constexpr int kSaveEveryTicks = 10;  // com o timer de 500 ms: salva o progresso a cada 5 s

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
    for (auto& item : items_) {
        if (running >= maxRunning_) break;
        if (!item->queued()) continue;
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
                              int connections) {
    auto item = std::make_unique<DownloadItem>();
    item->record.id = nextId_++;
    item->record.url = url;
    item->record.directory = dm::toUtf8(directory);
    item->record.fileName = dm::toUtf8(fileName);
    item->record.connections = connections;
    item->record.addedAt = unixNow();
    const uint64_t id = item->record.id;
    item->record.state = dm::RecordState::Queued;
    items_.push_back(std::move(item));
    advanceQueue();
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
    if (!items_.empty()) save();
}

bool DownloadManager::tick() {
    bool changed = false;
    for (auto& item : items_) {
        if (!item->task) continue;
        const dm::DownloadStatus previous = item->live.status;
        item->live = item->task->progress();
        dm::DownloadRecord& record = item->record;
        if (item->live.totalSize >= 0) record.totalSize = item->live.totalSize;
        if (item->live.status != dm::DownloadStatus::Connecting) record.downloaded = item->live.downloaded;
        if (!item->live.filePath.empty()) record.filePath = dm::toUtf8(item->live.filePath);

        if (item->live.status == previous) continue;
        changed = true;
        switch (item->live.status) {
            case dm::DownloadStatus::Completed:
                stopTask(*item);
                record.state = dm::RecordState::Completed;
                record.finishedAt = unixNow();
                record.errorCode = 0;
                if (onCompleted) onCompleted(*item);
                break;
            case dm::DownloadStatus::Failed:
                record.state = dm::RecordState::Failed;
                record.errorCode = static_cast<int>(item->live.error);
                record.errorDetail = item->live.errorDetail;
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

void DownloadManager::startTask(DownloadItem& item) {
    stopTask(item);
    dm::DownloadOptions options;
    options.url = item.record.url;
    options.connections = item.record.connections;
    options.speedLimit = item.record.speedLimit;
    options.sharedLimiter = globalLimiter_;
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
}

}  // namespace app
