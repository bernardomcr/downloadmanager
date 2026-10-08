#include "engine/debrid_task.h"

#include <windows.h>

#include <chrono>

#include "engine/http.h"
#include "util/file_io.h"
#include "util/unicode.h"
#include "version.h"

namespace dm {
namespace {

constexpr int kPollMilliseconds = 3000;
constexpr int kMaxNetworkRetries = 5;
constexpr size_t kMaxTorrentFile = 8 << 20;

DownloadError networkError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return DownloadError::NameNotResolved;
        case ERROR_WINHTTP_CANNOT_CONNECT: return DownloadError::CannotConnect;
        case ERROR_WINHTTP_TIMEOUT: return DownloadError::Timeout;
        case ERROR_WINHTTP_CONNECTION_ERROR: return DownloadError::ConnectionLost;
        case ERROR_WINHTTP_SECURE_FAILURE: return DownloadError::SecureConnection;
        default: return DownloadError::Network;
    }
}

}  // namespace

DebridTask::DebridTask(DebridTaskOptions options) : options_(std::move(options)) {
    progress_.resumable = true;  // pausar não perde nada: o torrent continua no Real-Debrid
    progress_.remoteId = options_.torrentId;
}

DebridTask::~DebridTask() {
    pause();
    wait();
}

void DebridTask::start() {
    {
        std::lock_guard lock(mutex_);
        if (progress_.status == DownloadStatus::Connecting || progress_.status == DownloadStatus::Downloading ||
            progress_.status == DownloadStatus::Completed) {
            return;
        }
    }
    wait();
    stopRequested_ = false;
    {
        std::lock_guard lock(mutex_);
        progress_.status = DownloadStatus::Connecting;
        progress_.remote = RemoteStage::Preparing;
        progress_.error = DownloadError::None;
        progress_.errorDetail = 0;
    }
    thread_ = std::thread(&DebridTask::run, this);
}

void DebridTask::pause() {
    stopRequested_ = true;
    wake_.notify_all();
}

void DebridTask::wait() {
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

DownloadProgress DebridTask::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

std::vector<DebridLink> DebridTask::links() const {
    std::lock_guard lock(mutex_);
    return links_;
}

bool DebridTask::sleep(int milliseconds) {
    std::unique_lock lock(sleepMutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [&] { return stopRequested_.load(); });
    return !stopRequested_;
}

void DebridTask::fail(DownloadError error, unsigned long detail) {
    std::lock_guard lock(mutex_);
    progress_.status = DownloadStatus::Failed;
    progress_.error = error;
    progress_.errorDetail = detail;
    progress_.bytesPerSecond = 0;
}

void DebridTask::run() {
    if (options_.token.empty()) return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::NotConnected));

    HttpSession session(L"DownloadManager/" + toWide(DM_VERSION_STRING));
    const std::vector<HttpHeader> auth{{"Authorization", "Bearer " + options_.token}};
    const std::string api = kRealDebridApi;
    int networkFailures = 0;

    // Chamada com novas tentativas para quedas de rede. nullopt: já falhou (ou pausou) e o estado foi definido.
    auto call = [&](const wchar_t* method, const std::string& path, const std::string& body,
                    const std::string& contentType) -> std::optional<ApiResponse> {
        for (;;) {
            if (stopRequested_) return std::nullopt;
            ApiResponse response = httpCall(session, method, api + path, auth, body, contentType);
            if (response.status == 0) {
                if (++networkFailures > kMaxNetworkRetries) {
                    fail(networkError(response.error), response.error);
                    return std::nullopt;
                }
                if (!sleep(kPollMilliseconds * networkFailures)) return std::nullopt;
                continue;
            }
            networkFailures = 0;
            if (response.status == 429 || response.status == 503) {  // devagar / instável: espera e repete
                if (!sleep(kPollMilliseconds * 2)) return std::nullopt;
                continue;
            }
            return response;
        }
    };
    auto failWith = [&](const ApiResponse& response) {
        fail(DownloadError::Debrid, static_cast<unsigned long>(parseDebridError(response.status, response.body)));
    };

    std::string id = options_.torrentId;
    bool resent = false;
    for (;;) {
        if (id.empty()) {
            std::optional<ApiResponse> response;
            if (!options_.magnet.empty()) {
                response = call(L"POST", "/torrents/addMagnet", formEncode({{"magnet", options_.magnet}}),
                                "application/x-www-form-urlencoded");
            } else {
                const auto file = readTextFile(options_.torrentFile);
                if (!file || file->empty() || file->size() > kMaxTorrentFile) {
                    return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::TorrentFailed));
                }
                response = call(L"PUT", "/torrents/addTorrent", *file, "application/x-bittorrent");
            }
            if (!response) break;
            if (response->status >= 300) return failWith(*response);
            id = parseAddedTorrentId(response->body);
            if (id.empty()) return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::Other));
            std::lock_guard lock(mutex_);
            progress_.remoteId = id;
        }

        const auto response = call(L"GET", "/torrents/info/" + id, {}, {});
        if (!response) break;
        if (response->status == 404 && !resent && (!options_.magnet.empty() || !options_.torrentFile.empty())) {
            id.clear();  // apagado no site do Real-Debrid: manda de novo
            resent = true;
            continue;
        }
        if (response->status >= 300) return failWith(*response);
        const auto torrent = parseDebridTorrent(response->body);
        if (!torrent) return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::Other));

        {
            std::lock_guard lock(mutex_);
            progress_.remoteName = torrent->fileName;
            progress_.totalSize = torrent->bytes;
            progress_.downloaded =
                torrent->bytes > 0 ? static_cast<int64_t>(static_cast<double>(torrent->bytes) * torrent->progress / 100.0) : 0;
            progress_.bytesPerSecond = static_cast<double>(torrent->speed);
            progress_.status = DownloadStatus::Downloading;
            switch (torrent->status) {
                case DebridTorrent::Status::Queued: progress_.remote = RemoteStage::Queued; break;
                case DebridTorrent::Status::Downloading: progress_.remote = RemoteStage::Downloading; break;
                case DebridTorrent::Status::Processing:
                case DebridTorrent::Status::Ready: progress_.remote = RemoteStage::Finishing; break;
                default: progress_.remote = RemoteStage::Preparing; break;
            }
        }

        switch (torrent->status) {
            case DebridTorrent::Status::Failed:
                return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::TorrentFailed));
            case DebridTorrent::Status::WaitingSelection: {
                // Todos os arquivos do torrent.
                const auto selected = call(L"POST", "/torrents/selectFiles/" + id, formEncode({{"files", "all"}}),
                                           "application/x-www-form-urlencoded");
                if (!selected) break;
                if (selected->status >= 300) return failWith(*selected);
                continue;
            }
            case DebridTorrent::Status::Ready: {
                std::vector<DebridLink> links;
                for (const std::string& link : torrent->links) {
                    const auto unrestricted = call(L"POST", "/unrestrict/link", formEncode({{"link", link}}),
                                                   "application/x-www-form-urlencoded");
                    if (!unrestricted) break;
                    if (unrestricted->status >= 300) return failWith(*unrestricted);
                    const auto parsed = parseDebridLink(unrestricted->body);
                    if (!parsed) return fail(DownloadError::Debrid, static_cast<unsigned long>(DebridError::Other));
                    links.push_back(*parsed);
                }
                if (links.size() != torrent->links.size()) break;  // pausou ou a rede caiu no meio
                std::lock_guard lock(mutex_);
                links_ = std::move(links);
                progress_.status = DownloadStatus::Completed;
                progress_.bytesPerSecond = 0;
                return;
            }
            default:
                if (!sleep(kPollMilliseconds)) break;
                continue;
        }
        break;  // pausado
    }

    std::lock_guard lock(mutex_);
    if (progress_.status != DownloadStatus::Failed && progress_.status != DownloadStatus::Completed) {
        progress_.status = DownloadStatus::Paused;
        progress_.bytesPerSecond = 0;
    }
}

}  // namespace dm
