#pragma once

#include <cstdint>
#include <string>

namespace dm {

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
    Protected,        // conteúdo com DRM: não é baixado
    ToolFailed,       // yt-dlp/ffmpeg falhou; detalhe em errorText
    WebPage,          // o link abre uma página (HTML), não um arquivo: provavelmente é um vídeo
    Debrid,           // Real-Debrid recusou; detalhe = DebridError
};

// Etapa de um torrent/magnet no Real-Debrid (antes de virar download direto).
enum class RemoteStage { None, Preparing, Queued, Downloading, Finishing };

struct DownloadProgress {
    DownloadStatus status = DownloadStatus::Idle;
    int64_t totalSize = -1;  // -1: desconhecido
    int64_t downloaded = 0;
    double bytesPerSecond = 0;
    int64_t secondsLeft = -1;  // quando a fonte informa (vídeos); senão a UI calcula
    int activeConnections = 0;
    bool resumable = false;  // pausar não perde o progresso
    std::wstring filePath;   // caminho final do arquivo
    DownloadError error = DownloadError::None;
    unsigned long errorDetail = 0;
    std::string errorText;   // mensagem da ferramenta (UTF-8), para ToolFailed
    int part = 0;            // vídeos: 1 = vídeo, 2 = áudio... (0 = não se aplica)
    bool postProcessing = false;  // vídeos: juntando/convertendo
    RemoteStage remote = RemoteStage::None;  // torrent: ainda no Real-Debrid
    std::string remoteId;    // id do torrent no Real-Debrid (guardado para não mandar de novo)
    std::string remoteName;  // nome do torrent informado pelo Real-Debrid
};

// Um download em andamento (arquivo comum ou vídeo). Métodos seguros para chamar da thread da interface.
class Task {
public:
    virtual ~Task() = default;
    // Começa ou retoma.
    virtual void start() = 0;
    // Para e guarda o que já foi baixado, para continuar depois.
    virtual void pause() = 0;
    // Espera a thread do download terminar (concluído, pausado ou com erro).
    virtual void wait() = 0;
    virtual DownloadProgress progress() const = 0;
    // Muda o limite deste download na hora. 0 = sem limite.
    virtual void setSpeedLimit(int64_t bytesPerSecond) = 0;
};

}  // namespace dm
