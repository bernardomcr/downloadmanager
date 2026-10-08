#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dm {

// Real-Debrid (api.real-debrid.com/rest/1.0): o torrent/magnet é baixado pelo serviço e volta como
// link direto, que o app baixa normalmente. Aqui só monta os pedidos e lê as respostas (sem rede).

inline constexpr const char* kRealDebridApi = "https://api.real-debrid.com/rest/1.0";

// Links aceitos como torrent: "magnet:?xt=urn:btih:..." (hash hex de 40 ou base32 de 32 caracteres).
bool isMagnetLink(const std::string& text);
// Nome legível do magnet (parâmetro dn), ou vazio.
std::string magnetDisplayName(const std::string& magnet);
// Hash do magnet em minúsculas (identifica o torrent), ou vazio.
std::string magnetHash(const std::string& magnet);

// Corpo application/x-www-form-urlencoded: {{"magnet", "..."}} -> "magnet=...".
std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);

enum class DebridError {
    None,
    NotConnected,     // sem token nas Configurações
    BadToken,         // token inválido ou expirado
    NotPremium,       // conta sem premium / bloqueada
    TorrentFailed,    // o Real-Debrid não conseguiu baixar (sem fontes, vírus, erro)
    TooManyTorrents,  // limite de torrents ativos da conta
    Unavailable,      // serviço fora do ar / hoster indisponível
    Other,
};

// Erro a partir do status HTTP e do JSON {"error": "...", "error_code": N}.
DebridError parseDebridError(int httpStatus, const std::string& body);

struct DebridUser {
    std::string username;
    bool premium = false;
    std::string expiration;  // "2026-12-31T00:00:00.000Z"
};
std::optional<DebridUser> parseDebridUser(const std::string& body);

// Resposta de /torrents/addMagnet e /torrents/addTorrent: {"id": "...", "uri": "..."}.
std::string parseAddedTorrentId(const std::string& body);

struct DebridTorrent {
    enum class Status { Converting, WaitingSelection, Queued, Downloading, Processing, Ready, Failed };

    std::string id;
    std::string fileName;
    int64_t bytes = -1;        // tamanho dos arquivos selecionados
    double progress = 0;       // 0..100 (download do lado do Real-Debrid)
    int64_t speed = 0;         // bytes/s no Real-Debrid
    int seeders = -1;
    Status status = Status::Converting;
    std::vector<std::string> links;  // prontos para /unrestrict/link quando status == Ready
};
std::optional<DebridTorrent> parseDebridTorrent(const std::string& body);

// Resposta de /unrestrict/link.
struct DebridLink {
    std::string download;  // link direto (https)
    std::string fileName;
    int64_t size = -1;
};
std::optional<DebridLink> parseDebridLink(const std::string& body);

// "2026-12-31T00:00:00.000Z" -> "31/12/2026" (ou "2026-12-31" em inglês); vazio se não reconhecer.
std::string formatDebridDate(const std::string& iso, bool portuguese);

}  // namespace dm
