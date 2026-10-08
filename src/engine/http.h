#pragma once

#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/http_headers.h"

namespace dm {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpResponse {
    int status = 0;
    int64_t contentLength = -1;
    std::optional<ContentRange> contentRange;
    std::string contentDisposition;
    std::string contentType;
    std::string etag;
    std::string lastModified;
    std::string finalUrl;  // depois de redirecionamentos
};

// Uma sessão WinHTTP por download: guarda proxy do sistema, timeouts e o pool de conexões.
class HttpSession {
public:
    explicit HttpSession(const std::wstring& userAgent);
    ~HttpSession();
    HttpSession(const HttpSession&) = delete;
    HttpSession& operator=(const HttpSession&) = delete;

    HINTERNET handle() const { return session_; }

private:
    HINTERNET session_ = nullptr;
};

// Um GET. Sincrono; abort() pode ser chamado de outra thread para destravar read().
class HttpRequest {
public:
    HttpRequest() = default;
    ~HttpRequest();
    HttpRequest(const HttpRequest&) = delete;
    HttpRequest& operator=(const HttpRequest&) = delete;

    // rangeFrom < 0: sem cabeçalho Range. rangeTo < 0: até o fim do arquivo.
    // Em caso de falha, `errorCode` recebe o código do Windows/WinHTTP.
    bool send(const HttpSession& session, const std::string& url, const std::vector<HttpHeader>& headers,
              int64_t rangeFrom, int64_t rangeTo, DWORD& errorCode);

    const HttpResponse& response() const { return response_; }

    // > 0: bytes lidos; 0: fim da resposta; < 0: erro (código em `errorCode`).
    int64_t read(void* buffer, size_t size, DWORD& errorCode);

    void abort();

private:
    void close();

    std::mutex mutex_;
    HINTERNET connection_ = nullptr;
    HINTERNET request_ = nullptr;
    HttpResponse response_;
};

// Pedido pequeno a uma API (JSON): método, corpo e a resposta inteira na memória (até 4 MB).
struct ApiResponse {
    int status = 0;     // 0: não chegou a resposta (erro em `error`)
    DWORD error = 0;
    std::string body;
};
ApiResponse httpCall(const HttpSession& session, const wchar_t* method, const std::string& url,
                     const std::vector<HttpHeader>& headers, const std::string& body = {},
                     const std::string& contentType = {});

}  // namespace dm
