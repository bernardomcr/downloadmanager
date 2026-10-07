#include "engine/http.h"

#include <charconv>

#include "util/unicode.h"

namespace dm {
namespace {

// Lê um cabeçalho da resposta pelo nome (mais portável que os índices WINHTTP_QUERY_*).
std::string queryHeader(HINTERNET request, const wchar_t* name) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    value.resize(size / sizeof(wchar_t));
    return toUtf8(value);
}

std::string queryUrl(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &size);
    if (size == 0) return {};
    std::wstring url(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, url.data(), &size)) return {};
    url.resize(size / sizeof(wchar_t));
    while (!url.empty() && url.back() == L'\0') url.pop_back();
    return toUtf8(url);
}

}  // namespace

HttpSession::HttpSession(const std::wstring& userAgent) {
    // Proxy automático do Windows (8.1+); cai para o proxy padrão em ambientes que não suportam.
    session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session_) {
        session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session_) {
        WinHttpSetTimeouts(session_, 15000, 15000, 30000, 30000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
        }
    }
}

HttpSession::~HttpSession() {
    if (session_) WinHttpCloseHandle(session_);
}

HttpRequest::~HttpRequest() {
    close();
}

void HttpRequest::close() {
    std::lock_guard lock(mutex_);
    if (request_) WinHttpCloseHandle(request_);
    if (connection_) WinHttpCloseHandle(connection_);
    request_ = nullptr;
    connection_ = nullptr;
}

void HttpRequest::abort() {
    // Fechar o handle cancela uma leitura síncrona em andamento em outra thread.
    close();
}

bool HttpRequest::send(const HttpSession& session, const std::string& url, const std::vector<HttpHeader>& headers,
                       int64_t rangeFrom, int64_t rangeTo, DWORD& errorCode) {
    response_ = {};
    if (!session.handle()) {
        errorCode = ERROR_WINHTTP_INTERNAL_ERROR;
        return false;
    }

    const std::wstring wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) {
        errorCode = ERROR_WINHTTP_INVALID_URL;
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    // Remove o fragmento (#...): não vai para o servidor.
    if (const size_t hash = path.find(L'#'); hash != std::wstring::npos) path.resize(hash);

    HINTERNET connection = WinHttpConnect(session.handle(), host.c_str(), parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    {
        std::lock_guard lock(mutex_);
        connection_ = connection;
        request_ = request;
    }
    if (!request) {
        errorCode = GetLastError();
        close();
        return false;
    }

    std::wstring extra = L"Accept-Encoding: identity\r\n";
    if (rangeFrom >= 0) {
        extra += L"Range: bytes=" + std::to_wstring(rangeFrom) + L"-";
        if (rangeTo >= 0) extra += std::to_wstring(rangeTo);
        extra += L"\r\n";
    }
    for (const auto& header : headers) {
        extra += toWide(header.name) + L": " + toWide(header.value) + L"\r\n";
    }

    if (!WinHttpSendRequest(request, extra.c_str(), static_cast<DWORD>(extra.size()), WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        errorCode = GetLastError();
        close();
        return false;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    response_.status = static_cast<int>(status);

    const std::string length = queryHeader(request, L"Content-Length");
    int64_t parsedLength = -1;
    if (std::from_chars(length.data(), length.data() + length.size(), parsedLength).ec == std::errc()) {
        response_.contentLength = parsedLength;
    }
    response_.contentRange = parseContentRange(queryHeader(request, L"Content-Range"));
    response_.contentDisposition = queryHeader(request, L"Content-Disposition");
    response_.contentType = queryHeader(request, L"Content-Type");
    response_.etag = queryHeader(request, L"ETag");
    response_.lastModified = queryHeader(request, L"Last-Modified");
    response_.finalUrl = queryUrl(request);
    if (response_.finalUrl.empty()) response_.finalUrl = url;
    return true;
}

int64_t HttpRequest::read(void* buffer, size_t size, DWORD& errorCode) {
    HINTERNET request;
    {
        std::lock_guard lock(mutex_);
        request = request_;
    }
    if (!request) {
        errorCode = ERROR_WINHTTP_OPERATION_CANCELLED;
        return -1;
    }
    DWORD received = 0;
    if (!WinHttpReadData(request, buffer, static_cast<DWORD>(size), &received)) {
        errorCode = GetLastError();
        return -1;
    }
    return received;
}

}  // namespace dm
