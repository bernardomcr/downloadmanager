#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace app {

// yt-dlp e ffmpeg ficam em %LOCALAPPDATA%\DownloadManager\tools. São baixados na primeira vez
// que um vídeo é pedido (com o próprio motor do app) e o yt-dlp se atualiza uma vez por semana.
class VideoTools {
public:
    enum class State { Missing, Installing, Ready, Failed };

    explicit VideoTools(std::wstring dataDirectory);
    ~VideoTools();

    bool ready() const;
    std::wstring ytDlpPath() const;
    std::wstring ffmpegDirectory() const { return toolsDirectory_; }
    std::wstring tempDirectory() const { return toolsDirectory_; }

    // Começa a instalação em segundo plano (se ainda não estiver pronta).
    void install();
    State state() const;
    // 0..1 durante a instalação.
    double installProgress() const { return progress_; }

    // Roda "yt-dlp -U" em segundo plano se a última atualização foi há mais de 7 dias.
    void updateIfStale();

private:
    void runInstall();

    std::wstring toolsDirectory_;
    std::thread worker_;
    std::thread updater_;
    std::atomic<State> state_{State::Missing};
    std::atomic<double> progress_{0};
    std::atomic<bool> stopping_{false};
    mutable std::mutex mutex_;
};

}  // namespace app
