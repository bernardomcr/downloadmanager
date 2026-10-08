#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/organizer.h"
#include "core/debrid.h"
#include "core/download_list.h"
#include "core/rules.h"
#include "core/video.h"
#include "engine/download_task.h"
#include "engine/task.h"

namespace app {

class VideoTools;

// Um item da lista. `record` é o que fica salvo; `task` só existe enquanto o download está rodando.
struct DownloadItem {
    dm::DownloadRecord record;
    std::unique_ptr<dm::Task> task;
    dm::DownloadProgress live;  // último progresso lido da tarefa
    bool forced = false;               // "Começar agora": ignora o limite da fila e o agendador
    bool pausedBySchedule = false;     // pausado pelo agendador: volta para a fila, não para "Pausado"
    bool rejectWebPages = false;       // link colado à mão: se for uma página, vira análise de vídeo
    bool organizing = false;           // concluído, sendo movido/extraído pela regra

    bool running() const {
        return task && (live.status == dm::DownloadStatus::Connecting ||
                        live.status == dm::DownloadStatus::Downloading);
    }
    bool completed() const { return record.state == dm::RecordState::Completed; }
    bool queued() const { return !task && record.state == dm::RecordState::Queued; }
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

    // Regras da fila. Aplicadas no próximo tick().
    void setMaxRunning(int count);
    void setSchedule(bool enabled, int startMinute, int endMinute);
    void setGlobalSpeedLimit(int64_t bytesPerSecond);

    // headers: cabeçalhos extras (Cookie, Referer) do navegador; guardados criptografados.
    uint64_t add(const std::string& url, const std::wstring& directory, const std::wstring& fileName,
                 int connections, const std::vector<std::pair<std::string, std::string>>& headers = {},
                 const std::string& userAgent = {}, bool rejectWebPages = false);
    // Vídeo (ou item de playlist) para o yt-dlp. title vira o nome do arquivo (vazio: título do site).
    uint64_t addVideo(const std::string& url, const std::wstring& directory, const std::wstring& title,
                      const dm::VideoFormat& format, bool subtitles,
                      const std::vector<std::pair<std::string, std::string>>& headers = {},
                      const std::string& userAgent = {});
    // Torrent pelo Real-Debrid: `source` é o magnet ou o caminho do .torrent já copiado para a pasta do app.
    // Quando o serviço termina, vira um download direto por arquivo do torrent.
    uint64_t addTorrent(const std::string& source, const std::wstring& directory, const std::wstring& displayName,
                        int connections);
    // Token da API (texto puro, só na memória). Vazio: torrents falham com "conecte o Real-Debrid".
    void setDebridToken(std::string token) { debridToken_ = std::move(token); }
    // Regras de organização (aplicadas ao concluir quem foi para a pasta padrão).
    void setRules(std::vector<dm::Rule> rules, bool enabled, std::wstring baseFolder);
    // Marca se um download novo deve ser organizado pelas regras (foi para a pasta padrão).
    void setOrganize(uint64_t id, bool organize);
    // Sem as ferramentas prontas, vídeos esperam na fila.
    void setVideoTools(VideoTools* tools) { videoTools_ = tools; }

    // Arquivo que o navegador baixou e o app organizou: entra direto em Concluídos.
    uint64_t addCompleted(const std::string& url, const std::wstring& filePath, int64_t size);
    // Continuar: começa se houver vaga (e o agendador deixar); senão entra na fila.
    void resume(uint64_t id);
    // Começa já, sem esperar vaga nem horário.
    void startNow(uint64_t id);
    void pause(uint64_t id);
    void setSpeedLimit(uint64_t id, int64_t bytesPerSecond);
    // deleteFiles: apaga o arquivo incompleto (.dmpart/.dmstate). Arquivos concluídos ficam.
    void remove(uint64_t id, bool deleteFiles);
    bool changeUrl(uint64_t id, const std::string& url);
    // Fechando o app: para tudo, mas lembra o que estava ativo para retomar na próxima vez.
    void shutdown();

    // Lê o progresso das tarefas e anda com a fila. Devolve true se algum item mudou de situação
    // (a lista precisa ser redesenhada).
    bool tick();

    int runningCount() const;
    // O agendador deixa baixar agora?
    bool scheduleOpen() const;
    // Nada baixando e nada esperando na fila.
    bool idle() const;

    DownloadItem* find(uint64_t id);
    const std::vector<std::unique_ptr<DownloadItem>>& items() const { return items_; }

    std::function<void(const DownloadItem&)> onCompleted;
    // A regra pediu para abrir o arquivo ou a pasta depois de organizar.
    std::function<void(const std::wstring& path, bool openFile, bool openFolder)> onOrganized;
    // Um link colado à mão era uma página web: o item é removido e a interface oferece a análise de vídeo.
    std::function<void(const dm::DownloadRecord&)> onWebPage;

private:
    void startTask(DownloadItem& item);
    void stopTask(DownloadItem& item);
    void enqueue(DownloadItem& item);
    void organizeIfNeeded(DownloadItem& item);
    bool collectOrganized();
    // Aplica o agendador e começa downloads da fila enquanto houver vaga. true se algo mudou.
    bool advanceQueue();
    // O Real-Debrid terminou: o item vira o primeiro link direto e os outros arquivos entram como itens novos.
    void finishDebrid(DownloadItem& item, const std::vector<dm::DebridLink>& links);
    // .torrent que o app guardou para este item (apaga quando não precisa mais).
    void deleteTorrentCopy(const dm::DownloadRecord& record);

    std::wstring listPath_;
    std::vector<std::unique_ptr<DownloadItem>> items_;
    uint64_t nextId_ = 1;
    int ticksSinceSave_ = 0;
    int maxRunning_ = 3;
    bool scheduleEnabled_ = false;
    int scheduleStart_ = 0;
    int scheduleEnd_ = 0;
    VideoTools* videoTools_ = nullptr;
    std::string debridToken_;
    std::vector<dm::Rule> rules_;
    bool rulesEnabled_ = true;
    std::wstring baseFolder_;
    Organizer organizer_;
    std::shared_ptr<dm::RateLimiter> globalLimiter_ = std::make_shared<dm::RateLimiter>();
};

}  // namespace app
