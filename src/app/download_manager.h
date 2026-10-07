#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/download_list.h"
#include "engine/download_task.h"

namespace app {

// Um item da lista. `record` é o que fica salvo; `task` só existe enquanto o download está rodando.
struct DownloadItem {
    dm::DownloadRecord record;
    std::unique_ptr<dm::DownloadTask> task;
    dm::DownloadProgress live;  // último progresso lido da tarefa

    bool running() const {
        return task && (live.status == dm::DownloadStatus::Connecting ||
                        live.status == dm::DownloadStatus::Downloading);
    }
    bool completed() const { return record.state == dm::RecordState::Completed; }
    double speed() const { return running() ? live.bytesPerSecond : 0.0; }
};

// Dono de todos os downloads. Usado só pela thread da interface; cada tarefa roda na sua própria thread.
class DownloadManager {
public:
    explicit DownloadManager(std::wstring listPath);
    ~DownloadManager();

    // Carrega a lista salva e retoma o que estava baixando quando o app fechou.
    void load();
    void save();

    uint64_t add(const std::string& url, const std::wstring& directory, const std::wstring& fileName,
                 int connections);
    void resume(uint64_t id);
    void pause(uint64_t id);
    // deleteFiles: apaga o arquivo incompleto (.dmpart/.dmstate). Arquivos concluídos ficam.
    void remove(uint64_t id, bool deleteFiles);
    bool changeUrl(uint64_t id, const std::string& url);
    // Fechando o app: para tudo, mas lembra o que estava ativo para retomar na próxima vez.
    void shutdown();

    // Lê o progresso das tarefas. Devolve true se algum item mudou de situação (a lista precisa ser redesenhada).
    bool tick();

    DownloadItem* find(uint64_t id);
    const std::vector<std::unique_ptr<DownloadItem>>& items() const { return items_; }

    std::function<void(const DownloadItem&)> onCompleted;

private:
    void startTask(DownloadItem& item);
    void stopTask(DownloadItem& item);

    std::wstring listPath_;
    std::vector<std::unique_ptr<DownloadItem>> items_;
    uint64_t nextId_ = 1;
    int ticksSinceSave_ = 0;
};

}  // namespace app
