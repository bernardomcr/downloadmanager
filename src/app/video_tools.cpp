#include "app/video_tools.h"

#include <windows.h>

#include "app/system.h"
#include "core/command_line.h"
#include "engine/download_task.h"
#include "engine/process.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

constexpr const char* kYtDlpUrl = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
// Builds do ffmpeg mantidos pelo próprio projeto yt-dlp (com os ajustes que ele precisa).
constexpr const char* kFfmpegUrl =
    "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";
constexpr int64_t kUpdateIntervalSeconds = 7 * 24 * 60 * 60;

// Baixa com o motor do app e espera terminar. `share`: quanto desta etapa vale no progresso total.
bool downloadFile(const std::string& url, const std::wstring& directory, const std::wstring& name,
                  std::atomic<double>& progress, double base, double share, const std::atomic<bool>& stopping) {
    dm::DownloadOptions options;
    options.url = url;
    options.directory = directory;
    options.fileName = name;
    dm::DownloadTask task(options);
    task.start();
    for (;;) {
        const dm::DownloadProgress state = task.progress();
        if (state.totalSize > 0) {
            progress = base + share * static_cast<double>(state.downloaded) / static_cast<double>(state.totalSize);
        }
        if (state.status == dm::DownloadStatus::Completed) return true;
        if (state.status == dm::DownloadStatus::Failed || state.status == dm::DownloadStatus::Paused || stopping) {
            return false;
        }
        Sleep(200);
    }
}

std::wstring systemTool(const wchar_t* name) {
    wchar_t directory[MAX_PATH];
    const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    return dm::joinPath(std::wstring(directory, length), name);
}

}  // namespace

VideoTools::VideoTools(std::wstring dataDirectory) : toolsDirectory_(dm::joinPath(dataDirectory, L"tools")) {
    CreateDirectoryW(toolsDirectory_.c_str(), nullptr);
    if (ready()) state_ = State::Ready;
}

VideoTools::~VideoTools() {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();
    if (updater_.joinable()) updater_.join();
}

bool VideoTools::ready() const {
    return dm::fileExists(ytDlpPath()) && dm::fileExists(dm::joinPath(toolsDirectory_, L"ffmpeg.exe"));
}

std::wstring VideoTools::ytDlpPath() const {
    return dm::joinPath(toolsDirectory_, L"yt-dlp.exe");
}

VideoTools::State VideoTools::state() const {
    return state_;
}

void VideoTools::install() {
    std::lock_guard lock(mutex_);
    if (state_ == State::Installing || ready()) {
        if (ready()) state_ = State::Ready;
        return;
    }
    if (worker_.joinable()) worker_.join();
    state_ = State::Installing;
    progress_ = 0;
    worker_ = std::thread(&VideoTools::runInstall, this);
}

void VideoTools::runInstall() {
    // yt-dlp: ~18 MB (10% do progresso); ffmpeg: ~140 MB compactado (80%); extração (10%).
    if (!dm::fileExists(ytDlpPath()) &&
        !downloadFile(kYtDlpUrl, toolsDirectory_, L"yt-dlp.exe", progress_, 0.0, 0.1, stopping_)) {
        state_ = State::Failed;
        return;
    }
    const std::wstring ffmpeg = dm::joinPath(toolsDirectory_, L"ffmpeg.exe");
    if (!dm::fileExists(ffmpeg)) {
        const std::wstring zip = dm::joinPath(toolsDirectory_, L"ffmpeg.zip");
        if (!dm::fileExists(zip) &&
            !downloadFile(kFfmpegUrl, toolsDirectory_, L"ffmpeg.zip", progress_, 0.1, 0.8, stopping_)) {
            state_ = State::Failed;
            return;
        }
        progress_ = 0.9;
        // tar.exe vem no Windows 10/11 e abre .zip; tira só os executáveis da pasta bin.
        const std::string command = dm::buildCommandLine(
            dm::toUtf8(systemTool(L"tar.exe")),
            {"-xf", dm::toUtf8(zip), "-C", dm::toUtf8(toolsDirectory_), "--strip-components", "2", "*/bin/ffmpeg.exe",
             "*/bin/ffprobe.exe"});
        std::string output;
        dm::runAndCapture(command, output);
        DeleteFileW(zip.c_str());
    }
    progress_ = 1.0;
    if (ready()) {
        dm::writeTextFileAtomically(dm::joinPath(toolsDirectory_, L"last-update.txt"), std::to_string(unixNow()));
        state_ = State::Ready;
    } else {
        state_ = State::Failed;
    }
}

void VideoTools::updateIfStale() {
    if (!dm::fileExists(ytDlpPath()) || updater_.joinable()) return;
    const std::wstring stamp = dm::joinPath(toolsDirectory_, L"last-update.txt");
    int64_t last = 0;
    if (const auto text = dm::readTextFile(stamp)) last = std::atoll(text->c_str());
    if (unixNow() - last < kUpdateIntervalSeconds) return;

    updater_ = std::thread([this, stamp] {
        std::string output;
        const int code = dm::runAndCapture(dm::buildCommandLine(dm::toUtf8(ytDlpPath()), {"-U"}), output);
        if (code == 0) dm::writeTextFileAtomically(stamp, std::to_string(unixNow()));
    });
}

}  // namespace app
