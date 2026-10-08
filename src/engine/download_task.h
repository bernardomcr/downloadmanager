#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
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
#include "engine/task.h"

namespace dm {

class SegmentPlanner;

// Enquanto baixa, o arquivo fica como "<nome>.dmpart" e o progresso em "<nome>.dmstate".
inline constexpr const wchar_t* kPartSuffix = L".dmpart";
inline constexpr const wchar_t* kStateSuffix = L".dmstate";

struct DownloadOptions {
    std::string url;
    std::wstring directory;
    std::wstring fileName;  // vazio: descobre pelo servidor/URL
    std::vector<HttpHeader> headers;
    int connections = 16;
    int64_t minSplitSize = 512 * 1024;  // não divide restos menores que 2x isso
    int maxRetries = 5;                 // falhas seguidas sem progresso antes de desistir
    int64_t speedLimit = 0;             // bytes/s só deste download; 0 = sem limite
    bool rejectWebPages = false;        // link colado pelo usuário: página HTML vira erro WebPage
    // Limite total, compartilhado por todos os downloads (opcional).
    std::shared_ptr<RateLimiter> sharedLimiter;
    // Vazio: User-Agent próprio do app (honesto, como curl/wget). Servidores com proteção contra robôs
    // derrubam quem diz ser o Chrome sem ser; se o servidor recusar, tenta uma vez com o de navegador.
    // Preenchido (veio do navegador): usado sempre, para os cookies baterem com a sessão.
    std::wstring userAgent;
};

// User-Agent do app e o de navegador usado como segunda tentativa.
std::wstring appUserAgent();
std::wstring browserLikeUserAgent();


// Um download HTTP/HTTPS. Thread própria; todos os métodos públicos são seguros para chamar da UI.
class DownloadTask : public Task {
public:
    explicit DownloadTask(DownloadOptions options);
    ~DownloadTask() override;
    DownloadTask(const DownloadTask&) = delete;
    DownloadTask& operator=(const DownloadTask&) = delete;

    // Começa ou retoma (reconsulta o servidor e continua de onde parou, se o arquivo for o mesmo).
    void start() override;
    // Para as conexões e salva o estado. O download pode ser retomado depois, mesmo após fechar o app.
    void pause() override;
    // Bloqueia até a thread do download terminar (concluído, pausado ou com erro).
    void wait() override;

    // Troca um link expirado mantendo o progresso. Só com o download parado; o próximo start()
    // confere se o novo link aponta para o mesmo arquivo.
    bool setUrl(const std::string& url);

    // Muda o limite deste download na hora, mesmo baixando. 0 = sem limite.
    void setSpeedLimit(int64_t bytesPerSecond) override;

    DownloadProgress progress() const override;

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
    // index: ordem de criação (a rampa pode mandar as últimas embora com activeLimit_).
    void worker(int index, std::optional<size_t> segment, std::unique_ptr<HttpRequest> request);
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
    std::atomic<int> activeLimit_{32};         // conexões com índice >= isso saem (rampa voltou atrás)
    std::function<void()> spawnWorker_;        // cria mais uma conexão (só durante runSegmented)
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
