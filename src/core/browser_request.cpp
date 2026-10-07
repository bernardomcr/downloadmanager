#include "core/browser_request.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "core/json.h"

namespace dm {
namespace {

constexpr size_t kMaxUrl = 16 * 1024;
constexpr size_t kMaxField = 64 * 1024;
constexpr size_t kMaxHeaders = 40;
constexpr size_t kMaxPath = 32 * 1024;

// Cabeçalhos que o WinHTTP controla ou que mudariam o significado do pedido.
bool isForbiddenHeader(const std::string& name) {
    static constexpr std::string_view kForbidden[] = {
        "host", "connection", "content-length", "range", "if-range", "accept-encoding", "transfer-encoding",
        "upgrade", "te", "trailer", "keep-alive", "expect", "proxy-authorization", "proxy-connection",
        "if-none-match", "if-modified-since", "user-agent", "cookie", "referer"};
    std::string lower;
    for (const char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto forbidden : kForbidden) {
        if (lower == forbidden) return true;
    }
    return false;
}

bool isTokenChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || std::string_view("!#$%&'*+.^_`|~-").find(c) != std::string_view::npos;
}

bool isValidHeaderName(const std::string& name) {
    return !name.empty() && name.size() <= 128 && std::all_of(name.begin(), name.end(), isTokenChar);
}

bool isWebUrl(const std::string& url) {
    auto startsWith = [&](std::string_view prefix) {
        return url.size() > prefix.size() &&
               std::equal(prefix.begin(), prefix.end(), url.begin(),
                          [](char a, char b) { return a == std::tolower(static_cast<unsigned char>(b)); });
    };
    return startsWith("http://") || startsWith("https://");
}

bool hasLineBreak(const std::string& text) {
    return text.find_first_of("\r\n") != std::string::npos;
}

}  // namespace

std::optional<BrowserRequest> parseBrowserRequest(const std::string& json) {
    const auto value = parseJson(json);
    if (!value || !value->isObject() || value->string("type") != "add") return std::nullopt;

    BrowserRequest request;
    request.url = value->string("url");
    request.fileName = value->string("fileName");
    request.referrer = value->string("referrer");
    request.cookies = value->string("cookies");
    request.userAgent = value->string("userAgent");
    request.token = value->string("token");
    if (request.token.size() > 64 || hasLineBreak(request.token)) request.token.clear();
    const std::string source = value->string("source");
    request.source = source == "link" ? BrowserRequest::Source::Link
                     : source == "media" ? BrowserRequest::Source::Media
                                         : BrowserRequest::Source::Capture;

    if (!isWebUrl(request.url) || request.url.size() > kMaxUrl || hasLineBreak(request.url)) return std::nullopt;
    // Quebras de linha num cabeçalho permitiriam injetar outros cabeçalhos: descarta o campo.
    for (std::string* field : {&request.fileName, &request.referrer, &request.cookies, &request.userAgent}) {
        if (field->size() > kMaxField || hasLineBreak(*field)) field->clear();
    }
    if (!request.referrer.empty() && !isWebUrl(request.referrer)) request.referrer.clear();

    if (const JsonValue* headers = value->field("headers"); headers && headers->array()) {
        for (const JsonValue& header : *headers->array()) {
            std::string name = header.string("name");
            std::string text = header.string("value");
            if (!isValidHeaderName(name) || isForbiddenHeader(name)) continue;
            if (text.size() > kMaxField || hasLineBreak(text)) continue;
            request.headers.emplace_back(std::move(name), std::move(text));
            if (request.headers.size() >= kMaxHeaders) break;
        }
    }
    // Cookie e Referer vindos da lista de cabeçalhos valem como os campos próprios.
    if (const JsonValue* headers = value->field("headers"); headers && headers->array()) {
        for (const JsonValue& header : *headers->array()) {
            std::string name = header.string("name");
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const std::string text = header.string("value");
            if (text.size() > kMaxField || hasLineBreak(text)) continue;
            if (name == "cookie" && request.cookies.empty()) request.cookies = text;
            if (name == "referer" && request.referrer.empty() && isWebUrl(text)) request.referrer = text;
        }
    }
    return request;
}

std::optional<AdoptRequest> parseAdoptRequest(const std::string& json) {
    const auto value = parseJson(json);
    if (!value || !value->isObject() || value->string("type") != "adopt") return std::nullopt;
    AdoptRequest request;
    request.path = value->string("path");
    request.url = value->string("url");
    const std::string& path = request.path;
    const bool driveLetter = path.size() > 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
                             (path[2] == '\\' || path[2] == '/');
    const bool network = path.size() > 2 && path[0] == '\\' && path[1] == '\\' && path[2] != '?' && path[2] != '.';
    if (!(driveLetter || network) || path.size() > kMaxPath || hasLineBreak(path)) return std::nullopt;
    // Nada de "..": o caminho tem que ser o arquivo exato que o navegador gravou.
    for (size_t start = 0; start <= path.size();) {
        size_t end = path.find_first_of("\\/", start);
        if (end == std::string::npos) end = path.size();
        if (path.compare(start, end - start, "..") == 0) return std::nullopt;
        start = end + 1;
    }
    if (!isWebUrl(request.url) || hasLineBreak(request.url) || request.url.size() > kMaxUrl) request.url.clear();
    return request;
}

std::string serializeAdoptRequest(const AdoptRequest& request) {
    JsonValue::Object object;
    object["type"] = "adopt";
    object["path"] = request.path;
    object["url"] = request.url;
    return JsonValue(std::move(object)).serialize();
}

std::string serializeBrowserRequest(const BrowserRequest& request) {
    JsonValue::Object object;
    object["type"] = "add";
    object["url"] = request.url;
    object["fileName"] = request.fileName;
    object["referrer"] = request.referrer;
    object["cookies"] = request.cookies;
    object["userAgent"] = request.userAgent;
    object["token"] = request.token;
    JsonValue::Array headers;
    for (const auto& [name, value] : request.headers) {
        headers.push_back(JsonValue::Object{{"name", JsonValue(name)}, {"value", JsonValue(value)}});
    }
    object["headers"] = std::move(headers);
    object["source"] = request.source == BrowserRequest::Source::Link    ? "link"
                       : request.source == BrowserRequest::Source::Media ? "media"
                                                                          : "capture";
    return JsonValue(std::move(object)).serialize();
}

std::vector<std::pair<std::string, std::string>> browserHeaders(const BrowserRequest& request) {
    std::vector<std::pair<std::string, std::string>> headers;
    if (!request.cookies.empty()) headers.emplace_back("Cookie", request.cookies);
    if (!request.referrer.empty()) headers.emplace_back("Referer", request.referrer);
    headers.insert(headers.end(), request.headers.begin(), request.headers.end());
    return headers;
}

}  // namespace dm
