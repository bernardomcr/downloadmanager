#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/debrid.h"
#include "engine/task.h"

namespace dm {

struct DebridTaskOptions {
    std::string token;          // token da API do Real-Debrid (já descriptografado)
    std::string magnet;         // magnet:?... ou vazio se for arquivo
    std::wstring torrentFile;   // .torrent guardado pelo app
    std::string torrentId;      // já enviado antes: só acompanha
};

// Torrent/magnet no Real-Debrid: envia, escolhe todos os arquivos, acompanha o download do lado do
// serviço e, quando fica pronto, pega os links diretos. Termina como Completed com links() preenchido;
// quem baixa os arquivos é um DownloadTask comum (o DownloadManager troca um pelo outro).
class DebridTask : public Task {
public:
    explicit DebridTask(DebridTaskOptions options);
    ~DebridTask() override;

    void start() override;
    void pause() override;
    void wait() override;
    DownloadProgress progress() const override;
    void setSpeedLimit(int64_t) override {}

    // Links diretos, depois de Completed.
    std::vector<DebridLink> links() const;

private:
    void run();
    void fail(DownloadError error, unsigned long detail);
    // Espera `milliseconds` ou até pause(). false: pausado.
    bool sleep(int milliseconds);

    DebridTaskOptions options_;
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::mutex sleepMutex_;
    std::condition_variable wake_;

    mutable std::mutex mutex_;
    DownloadProgress progress_;
    std::vector<DebridLink> links_;
};

}  // namespace dm
