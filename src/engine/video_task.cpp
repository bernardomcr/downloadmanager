#include "engine/video_task.h"

#include <windows.h>

#include "core/command_line.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace dm {
namespace {

// Nome aleatório para o arquivo de cookies (fica só enquanto o yt-dlp roda).
std::wstring temporaryCookiesPath(const std::wstring& directory) {
    wchar_t name[MAX_PATH];
    if (!GetTempFileNameW(directory.c_str(), L"dmc", 0, name)) return {};
    return name;
}

}  // namespace

VideoTask::VideoTask(VideoTaskOptions options) : options_(std::move(options)) {
    progress_.resumable = true;
}

VideoTask::~VideoTask() {
    pause();
    wait();
}

void VideoTask::start() {
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
        progress_.error = DownloadError::None;
        progress_.errorText.clear();
        progress_.part = 0;
        progress_.postProcessing = false;
        lastError_.clear();
        lastDownloaded_ = 0;
    }
    thread_ = std::thread(&VideoTask::run, this);
}

void VideoTask::pause() {
    stopRequested_ = true;
    process_.kill();
}

void VideoTask::wait() {
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

DownloadProgress VideoTask::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}

void VideoTask::setSpeedLimit(int64_t bytesPerSecond) {
    // O yt-dlp só lê o limite ao começar: vale a partir da próxima vez que o download for iniciado.
    std::lock_guard lock(mutex_);
    options_.job.speedLimit = bytesPerSecond;
}

void VideoTask::run() {
    VideoJob job;
    {
        std::lock_guard lock(mutex_);
        job = options_.job;
    }

    std::wstring cookiesPath;
    if (!options_.cookieHeader.empty()) {
        cookiesPath = temporaryCookiesPath(options_.tempDirectory);
        if (!cookiesPath.empty() &&
            writeTextFileAtomically(cookiesPath, netscapeCookies(job.url, options_.cookieHeader))) {
            job.cookiesFile = toUtf8(cookiesPath);
        }
    }

    const std::string command = buildCommandLine(toUtf8(options_.ytDlpPath), ytDlpArguments(job));
    int exitCode = -1;
    if (process_.start(command, toWide(job.outputDirectory))) {
        // A pausa pode ter chegado entre start() e aqui.
        if (stopRequested_) process_.kill();
        exitCode = process_.run([this](const std::string& line) { handleLine(line); });
    } else {
        std::lock_guard lock(mutex_);
        lastError_ = "yt-dlp.exe";
    }
    if (!cookiesPath.empty()) DeleteFileW(cookiesPath.c_str());

    if (stopRequested_) {
        finish(DownloadStatus::Paused);
        return;
    }
    std::lock_guard lock(mutex_);
    if (exitCode == 0 && !finalPath_.empty()) {
        progress_.filePath = toWide(finalPath_);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (GetFileAttributesExW(progress_.filePath.c_str(), GetFileExInfoStandard, &info)) {
            progress_.totalSize = (static_cast<int64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
            progress_.downloaded = progress_.totalSize;
        }
        progress_.status = DownloadStatus::Completed;
    } else {
        progress_.error = isDrmError(lastError_) ? DownloadError::Protected : DownloadError::ToolFailed;
        progress_.errorText = lastError_.empty() ? "exit " + std::to_string(exitCode) : lastError_;
        progress_.status = DownloadStatus::Failed;
    }
    progress_.bytesPerSecond = 0;
    progress_.postProcessing = false;
}

void VideoTask::finish(DownloadStatus status) {
    std::lock_guard lock(mutex_);
    progress_.status = status;
    progress_.bytesPerSecond = 0;
    progress_.postProcessing = false;
}

void VideoTask::handleLine(const std::string& line) {
    std::lock_guard lock(mutex_);
    if (const auto update = parseYtDlpProgress(line)) {
        // Vídeo e áudio vêm separados: quando o contador volta para perto de zero, começou a próxima parte.
        if (progress_.part == 0 || update->downloaded + 1024 * 1024 < lastDownloaded_) ++progress_.part;
        lastDownloaded_ = update->downloaded;
        progress_.status = DownloadStatus::Downloading;
        progress_.downloaded = update->downloaded;
        progress_.totalSize = update->total;
        progress_.bytesPerSecond = update->speed;
        progress_.secondsLeft = update->eta;
        progress_.postProcessing = false;
    } else if (const auto path = parseYtDlpFinalPath(line)) {
        finalPath_ = *path;
    } else if (isYtDlpPostProcessing(line)) {
        progress_.postProcessing = true;
        progress_.bytesPerSecond = 0;
    } else if (line.rfind("ERROR:", 0) == 0) {
        lastError_ = line.substr(6);
        while (!lastError_.empty() && lastError_.front() == ' ') lastError_.erase(lastError_.begin());
    }
}

}  // namespace dm
