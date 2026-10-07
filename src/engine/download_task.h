#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/rate_limiter.h"
#include "core/resume_state.h"
#include "engine/http.h"
#include "engine/output_file.h"

namespace dm {

class SegmentPlanner;

// Enquanto baixa, o arquivo fica como "<nome>.dmpart" e o progresso em "<nome>.dmstate".
inline constexpr const wchar_t* kPartSuffix = L".dmpart";
inline constexpr const wchar_t* kStateSuffix = L".dmstate";

enum class DownloadStatus { Idle, Connecting, Downloading, Paused, Completed, Failed };

// Motivo da falha. A UI traduz; `errorDetail` traz o status HTTP ou o código do Windows.
enum class DownloadError {
    None,
    InvalidUrl,
    NameNotResolved,
    CannotConnect,
    Timeout,
    ConnectionLost,
    SecureConnection,
    HttpStatus,       // 4xx/5xx que não indicam link expirado
    LinkExpired,      // 401/403/404/410 com o download em andamento: trocar o link resolve
    ServerChanged,    // o arquivo no servidor mudou (tamanho/ETag); não dá para continuar
    DiskFull,
    AccessDenied,
    FileSystem,
    Network,          // outros erros de rede
};

struct DownloadOptions {
    std::string url;
    std::wstring directory;
    std::wstring fileName;  // vazio: descobre pelo servidor/URL
    std::vector<HttpHeader> headers;
    int connections = 8;
    int64_t minSplitSize = 512 * 1024;  // não divide restos menores que 2x isso
    int maxRetries = 5;                 // falhas seguidas sem progresso antes de desistir
    int64_t speedLimit = 0;             // bytes/s só deste download; 0 = sem limite
    // Limite total, compartilhado por todos os downloads (opcional).
    std::shared_ptr<RateLimiter> sharedLimiter;
    std::wstring userAgent =
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/130.0.0.0 Safari/537.36";
};

struct DownloadProgress {
    DownloadStatus status = DownloadStatus::Idle;
    int64_t totalSize = -1;  // -1: desconhecido
    int64_t downloaded = 0;
    double bytesPerSecond = 0;
    int activeConnections = 0;
    bool resumable = false;  // o servidor aceita pedaços (pausar não perde o progresso)
    std::wstring filePath;   // caminho final do arquivo
    DownloadError error = DownloadError::None;
    unsigned long errorDetail = 0;
};

// Um download HTTP/HTTPS. Thread própria; todos os métodos públicos são seguros para chamar da UI.
class DownloadTask {
public:
    explicit DownloadTask(DownloadOptions options);
    ~DownloadTask();
    DownloadTask(const DownloadTask&) = delete;
    DownloadTask& operator=(const DownloadTask&) = delete;

    // Começa ou retoma (reconsulta o servidor e continua de onde parou, se o arquivo for o mesmo).
    void start();
    // Para as conexões e salva o estado. O download pode ser retomado depois, mesmo após fechar o app.
    void pause();
    // Bloqueia até a thread do download terminar (concluído, pausado ou com erro).
    void wait();

    // Troca um link expirado mantendo o progresso. Só com o download parado; o próximo start()
    // confere se o novo link aponta para o mesmo arquivo.
    bool setUrl(const std::string& url);

    // Muda o limite deste download na hora, mesmo baixando. 0 = sem limite.
    void setSpeedLimit(int64_t bytesPerSecond);

    DownloadProgress progress() const;

private:
    // Quanto ler por vez: menos quando há limite, para a velocidade ficar estável.
    size_t readSize(size_t bufferSize) const;
    // Depois de receber `bytes`, espera o necessário para respeitar os limites. false se mandaram parar.
    bool throttle(int64_t bytes);

    void run();
    bool probe(HttpRequest& request);
    bool choosePaths(int64_t totalSize, const std::string& etag, const std::string& lastModified,
                     const std::string& suggestedName);
    void runSegmented(std::unique_ptr<HttpRequest> probeRequest, bool resuming);
    void runSingleStream(std::unique_ptr<HttpRequest> probeRequest);
    void worker(std::optional<size_t> segment, std::unique_ptr<HttpRequest> request);
    void monitor();
    void saveState();
    bool finalizeFile();

    void registerRequest(HttpRequest* request);
    void unregisterRequest(HttpRequest* request);
    void stopAll();
    void fail(DownloadError error, unsigned long detail);
    void failNetwork(DWORD code);
    void failFile(DWORD code);
    bool waitOrStop(std::chrono::milliseconds duration);
    void setStatus(DownloadStatus status);

    DownloadOptions options_;
    std::thread thread_;

    mutable std::mutex mutex_;  // protege os campos abaixo e options_.url
    DownloadStatus status_ = DownloadStatus::Idle;
    DownloadError error_ = DownloadError::None;
    unsigned long errorDetail_ = 0;
    std::wstring targetPath_;
    std::wstring partPath_;
    std::wstring statePath_;
    std::string etag_;
    std::string lastModified_;
    std::optional<ResumeState> resumeState_;
    std::string currentUrl_;  // só a thread do download usa

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<int64_t> totalSize_{-1};
    std::atomic<int64_t> downloaded_{0};
    std::atomic<double> speed_{0};
    std::atomic<bool> resumable_{false};

    std::atomic<int> runningWorkers_{0};
    std::mutex stopMutex_;
    std::condition_variable stopSignal_;
    mutable std::mutex requestsMutex_;
    std::vector<HttpRequest*> activeRequests_;

    RateLimiter ownLimiter_;
    std::unique_ptr<HttpSession> session_;
    std::unique_ptr<SegmentPlanner> planner_;
    OutputFile file_;
};

}  // namespace dm
