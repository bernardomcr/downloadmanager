#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dm {

// Situação salva de um download na lista do app.
enum class RecordState {
    Active,     // estava baixando quando o app fechou: volta sozinho
    Queued,     // esperando vaga na fila ou o horário do agendador
    Paused,
    Failed,
    Completed,
};

// Um item da lista de downloads como fica salvo em disco. Strings em UTF-8.
struct DownloadRecord {
    uint64_t id = 0;
    std::string url;
    std::string directory;
    std::string fileName;   // nome escolhido (vazio: descobre pelo servidor)
    std::string filePath;   // caminho final, depois de conhecido
    RecordState state = RecordState::Paused;
    int64_t totalSize = -1;
    int64_t downloaded = 0;
    int64_t addedAt = 0;     // segundos Unix
    int64_t finishedAt = 0;  // segundos Unix
    int connections = 8;
    int64_t speedLimit = 0;  // bytes/s; 0 = sem limite
    // Cabeçalhos extras (Cookie, Referer) vindos do navegador, já criptografados pelo app (opaco aqui).
    std::string protectedHeaders;
    std::string userAgent;   // do navegador que mandou o download; vazio = padrão do app

    bool isVideo = false;        // baixado pelo yt-dlp
    std::string videoFormat;     // VideoFormat::serialize(): "best", "1080", "mp3"...
    bool subtitles = false;
    std::string errorText;       // mensagem da ferramenta quando o erro é ToolFailed
    bool organize = false;       // foi para a pasta padrão: as regras escolhem a pasta final ao concluir
    // Torrent pelo Real-Debrid: `url` é o magnet ou o caminho do .torrent guardado pelo app.
    // Quando o serviço termina, o item vira um download direto comum (debrid volta a false).
    bool debrid = false;
    std::string debridId;        // id do torrent no Real-Debrid, depois de enviado
    int errorCode = 0;       // DownloadError salvo como número
    unsigned long errorDetail = 0;
};

std::string serializeDownloadList(const std::vector<DownloadRecord>& records);
// Linhas inválidas são ignoradas: um arquivo parcialmente corrompido não perde a lista inteira.
std::vector<DownloadRecord> parseDownloadList(const std::string& text);

}  // namespace dm
