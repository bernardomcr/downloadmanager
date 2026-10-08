#include "core/debrid.h"

#include <cctype>
#include <cstdio>

#include "core/json.h"

namespace dm {
namespace {

bool startsWithNoCase(const std::string& text, const char* prefix) {
    size_t i = 0;
    for (; prefix[i]; ++i) {
        if (i >= text.size() || std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
    }
    return true;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percentDecode(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            result += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0) {
            result += static_cast<char>(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
            i += 2;
        } else {
            result += text[i];
        }
    }
    return result;
}

// Valor do primeiro parâmetro `name` do magnet (já decodificado), ou vazio.
std::string magnetParameter(const std::string& magnet, const std::string& name) {
    const size_t query = magnet.find('?');
    if (query == std::string::npos) return {};
    size_t start = query + 1;
    while (start < magnet.size()) {
        size_t end = magnet.find('&', start);
        if (end == std::string::npos) end = magnet.size();
        const std::string pair = magnet.substr(start, end - start);
        const size_t equals = pair.find('=');
        if (equals != std::string::npos && pair.compare(0, equals, name) == 0) {
            return percentDecode(pair.substr(equals + 1));
        }
        start = end + 1;
    }
    return {};
}

DebridTorrent::Status statusFrom(const std::string& text) {
    using Status = DebridTorrent::Status;
    if (text == "magnet_conversion") return Status::Converting;
    if (text == "waiting_files_selection") return Status::WaitingSelection;
    if (text == "queued") return Status::Queued;
    if (text == "downloading") return Status::Downloading;
    if (text == "compressing" || text == "uploading") return Status::Processing;
    if (text == "downloaded") return Status::Ready;
    return Status::Failed;  // magnet_error, error, virus, dead e o que vier de novo
}

}  // namespace

bool isMagnetLink(const std::string& text) {
    if (!startsWithNoCase(text, "magnet:?") || text.size() > 16 * 1024) return false;
    if (text.find_first_of("\r\n\"<> ") != std::string::npos) return false;
    return !magnetHash(text).empty();
}

std::string magnetHash(const std::string& magnet) {
    if (!startsWithNoCase(magnet, "magnet:?")) return {};
    const std::string xt = magnetParameter(magnet, "xt");
    if (!startsWithNoCase(xt, "urn:btih:")) return {};
    std::string hash = xt.substr(9);
    for (char& c : hash) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (hash.size() == 40) {
        for (const char c : hash) {
            if (hexValue(c) < 0) return {};
        }
        return hash;
    }
    if (hash.size() == 32) {  // base32
        for (const char c : hash) {
            if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) return {};
        }
        return hash;
    }
    return {};
}

std::string magnetDisplayName(const std::string& magnet) {
    std::string name = magnetParameter(magnet, "dn");
    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    return name.size() > 300 ? name.substr(0, 300) : name;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string body;
    for (const auto& [name, value] : fields) {
        if (!body.empty()) body += '&';
        body += name + '=';
        for (const unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                body += static_cast<char>(c);
            } else {
                body += '%';
                body += kHex[c >> 4];
                body += kHex[c & 15];
            }
        }
    }
    return body;
}

DebridError parseDebridError(int httpStatus, const std::string& body) {
    if (httpStatus >= 200 && httpStatus < 300) return DebridError::None;
    int code = 0;
    if (const auto value = parseJson(body); value && value->isObject()) {
        if (const auto number = value->number("error_code")) code = static_cast<int>(*number);
    }
    switch (code) {
        case 8: return DebridError::BadToken;
        case 9: case 14: case 15: case 20: case 22: case 23: case 36: return DebridError::NotPremium;
        case 21: case 34: return DebridError::TooManyTorrents;
        case 6: case 16: case 17: case 18: case 19: case 25: return DebridError::Unavailable;
        case 24: case 26: case 27: case 28: case 29: case 30: case 35: return DebridError::TorrentFailed;
        default: break;
    }
    if (httpStatus == 401) return DebridError::BadToken;
    if (httpStatus == 403) return DebridError::NotPremium;
    if (httpStatus == 503 || httpStatus == 502) return DebridError::Unavailable;
    return DebridError::Other;
}

std::optional<DebridUser> parseDebridUser(const std::string& body) {
    const auto value = parseJson(body);
    if (!value || !value->isObject()) return std::nullopt;
    DebridUser user;
    user.username = value->string("username");
    if (user.username.empty()) return std::nullopt;
    user.premium = value->string("type") == "premium";
    user.expiration = value->string("expiration");
    return user;
}

std::string parseAddedTorrentId(const std::string& body) {
    const auto value = parseJson(body);
    if (!value || !value->isObject()) return {};
    std::string id = value->string("id");
    for (const char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return {};  // vai para a URL dos próximos pedidos
    }
    return id;
}

std::optional<DebridTorrent> parseDebridTorrent(const std::string& body) {
    const auto value = parseJson(body);
    if (!value || !value->isObject()) return std::nullopt;
    DebridTorrent torrent;
    torrent.id = value->string("id");
    if (torrent.id.empty()) return std::nullopt;
    torrent.fileName = value->string("filename");
    torrent.status = statusFrom(value->string("status"));
    if (const auto bytes = value->number("bytes"); bytes && *bytes > 0) torrent.bytes = static_cast<int64_t>(*bytes);
    if (const auto progress = value->number("progress")) torrent.progress = *progress < 0 ? 0 : *progress > 100 ? 100 : *progress;
    if (const auto speed = value->number("speed"); speed && *speed > 0) torrent.speed = static_cast<int64_t>(*speed);
    if (const auto seeders = value->number("seeders")) torrent.seeders = static_cast<int>(*seeders);
    if (const JsonValue* links = value->field("links"); links && links->array()) {
        for (const JsonValue& link : *links->array()) {
            if (const std::string* text = link.text(); text && startsWithNoCase(*text, "https://")) {
                torrent.links.push_back(*text);
            }
        }
    }
    if (torrent.status == DebridTorrent::Status::Ready && torrent.links.empty()) {
        torrent.status = DebridTorrent::Status::Failed;
    }
    return torrent;
}

std::optional<DebridLink> parseDebridLink(const std::string& body) {
    const auto value = parseJson(body);
    if (!value || !value->isObject()) return std::nullopt;
    DebridLink link;
    link.download = value->string("download");
    if (!startsWithNoCase(link.download, "https://") && !startsWithNoCase(link.download, "http://")) {
        return std::nullopt;
    }
    if (link.download.find_first_of("\r\n") != std::string::npos) return std::nullopt;
    link.fileName = value->string("filename");
    if (const auto size = value->number("filesize"); size && *size > 0) link.size = static_cast<int64_t>(*size);
    return link;
}

std::string formatDebridDate(const std::string& iso, bool portuguese) {
    auto digits = [&](size_t from, size_t count) {
        int value = 0;
        for (size_t i = from; i < from + count; ++i) {
            if (i >= iso.size() || !std::isdigit(static_cast<unsigned char>(iso[i]))) return -1;
            value = value * 10 + (iso[i] - '0');
        }
        return value;
    };
    const int year = digits(0, 4);
    const int month = digits(5, 2);
    const int day = digits(8, 2);
    if (year < 0 || iso[4] != '-' || iso[7] != '-' || month < 1 || month > 12 || day < 1 || day > 31) return {};
    char text[16];
    if (portuguese) {
        std::snprintf(text, sizeof(text), "%02d/%02d/%04d", day, month, year);
    } else {
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, day);
    }
    return text;
}

}  // namespace dm
