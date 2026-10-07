#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dm {

// Pedido de download vindo da extensão do navegador. Strings em UTF-8.
struct BrowserRequest {
    enum class Source { Capture, Link, Media };

    std::string url;
    std::string fileName;   // nome sugerido pelo navegador (pode ser vazio)
    std::string referrer;
    std::string cookies;    // cabeçalho Cookie pronto
    std::string userAgent;
    // Outros cabeçalhos que o navegador mandou no pedido original (Authorization, tokens do site...).
    std::vector<std::pair<std::string, std::string>> headers;
    std::string token;      // identifica o pedido para a extensão perguntar o andamento
    Source source = Source::Capture;
};

// Arquivo que o navegador terminou de baixar e o app deve organizar.
struct AdoptRequest {
    std::string path;  // caminho completo no Windows (UTF-8)
    std::string url;
};

// Valida a mensagem {"type":"add", "url":..., ...}. Só aceita http/https e limita tamanhos,
// porque o conteúdo vem de páginas da internet.
std::optional<BrowserRequest> parseBrowserRequest(const std::string& json);
std::string serializeBrowserRequest(const BrowserRequest& request);

// {"type":"adopt","path":"C:\\...","url":...}. Exige caminho absoluto do Windows, sem "..".
std::optional<AdoptRequest> parseAdoptRequest(const std::string& json);
std::string serializeAdoptRequest(const AdoptRequest& request);

// Cabeçalhos HTTP a enviar no download (Cookie, Referer e os extras do navegador).
std::vector<std::pair<std::string, std::string>> browserHeaders(const BrowserRequest& request);

}  // namespace dm
