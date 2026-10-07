#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/video.h"
#include "engine/process.h"
#include "engine/task.h"

namespace dm {

struct VideoTaskOptions {
    VideoJob job;               // o que baixar e como (strings UTF-8)
    std::wstring ytDlpPath;
    std::string cookieHeader;   // do navegador; vira um arquivo de cookies temporário
    std::wstring tempDirectory; // onde criar o arquivo de cookies
};

// Vídeo baixado pelo yt-dlp. Pausar mata o processo; continuar roda de novo e o yt-dlp
// aproveita os pedaços já baixados (--continue).
class VideoTask : public Task {
public:
    explicit VideoTask(VideoTaskOptions options);
    ~VideoTask() override;

    void start() override;
    void pause() override;
    void wait() override;
    DownloadProgress progress() const override;
    void setSpeedLimit(int64_t bytesPerSecond) override;

private:
    void run();
    void handleLine(const std::string& line);
    void finish(DownloadStatus status);

    VideoTaskOptions options_;
    std::thread thread_;
    Process process_;
    std::atomic<bool> stopRequested_{false};

    mutable std::mutex mutex_;
    DownloadProgress progress_;
    std::string lastError_;
    std::string finalPath_;
    int64_t lastDownloaded_ = 0;
};

}  // namespace dm
