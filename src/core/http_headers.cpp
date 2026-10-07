#include "core/http_headers.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string_view>

namespace dm {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::optional<int64_t> toInt(std::string_view text) {
    text = trim(text);
    int64_t value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string unquote(std::string_view value) {
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return std::string(value);
    std::string result;
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        if (value[i] == '\\' && i + 2 < value.size()) ++i;
        result += value[i];
    }
    return result;
}

}  // namespace

std::optional<ContentRange> parseContentRange(const std::string& header) {
    std::string_view text = trim(header);
    constexpr std::string_view kUnit = "bytes";
    if (text.size() < kUnit.size() || !equalsIgnoreCase(text.substr(0, kUnit.size()), kUnit)) return std::nullopt;
    text = trim(text.substr(kUnit.size()));

    const size_t dash = text.find('-');
    const size_t slash = text.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || dash > slash) return std::nullopt;

    const auto first = toInt(text.substr(0, dash));
    const auto last = toInt(text.substr(dash + 1, slash - dash - 1));
    if (!first || !last || *last < *first) return std::nullopt;

    ContentRange range{*first, *last, -1};
    const std::string_view total = trim(text.substr(slash + 1));
    if (total != "*") {
        const auto value = toInt(total);
        if (!value || *value <= *last) return std::nullopt;
        range.total = *value;
    }
    return range;
}

std::string percentDecode(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int high = hexValue(text[i + 1]);
            const int low = hexValue(text[i + 2]);
            if (high >= 0 && low >= 0) {
                result += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }
        result += text[i];
    }
    return result;
}

std::string fileNameFromContentDisposition(const std::string& header) {
    std::string plain;
    std::string extended;

    std::string_view rest = header;
    while (!rest.empty()) {
        // Separa parâmetros por ';' respeitando aspas.
        size_t end = 0;
        bool quoted = false;
        for (; end < rest.size(); ++end) {
            if (rest[end] == '"' && (end == 0 || rest[end - 1] != '\\')) quoted = !quoted;
            if (rest[end] == ';' && !quoted) break;
        }
        const std::string_view part = trim(rest.substr(0, end));
        rest = end < rest.size() ? rest.substr(end + 1) : std::string_view{};

        const size_t equals = part.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = trim(part.substr(0, equals));
        const std::string_view value = trim(part.substr(equals + 1));

        if (equalsIgnoreCase(key, "filename*")) {
            // charset'idioma'valor-codificado
            const size_t firstQuote = value.find('\'');
            const size_t secondQuote = firstQuote == std::string_view::npos ? firstQuote : value.find('\'', firstQuote + 1);
            if (secondQuote != std::string_view::npos) {
                extended = percentDecode(std::string(value.substr(secondQuote + 1)));
            }
        } else if (equalsIgnoreCase(key, "filename")) {
            plain = unquote(value);
        }
    }
    return extended.empty() ? plain : extended;
}

std::string fileNameFromUrl(const std::string& url) {
    std::string_view path = url;
    if (const size_t scheme = path.find("://"); scheme != std::string_view::npos) {
        path.remove_prefix(scheme + 3);
        const size_t slash = path.find('/');
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash);
    }
    path = path.substr(0, path.find_first_of("?#"));
    const size_t lastSlash = path.find_last_of('/');
    const std::string_view name = lastSlash == std::string_view::npos ? path : path.substr(lastSlash + 1);
    return percentDecode(std::string(name));
}

std::string sanitizeFileName(const std::string& name) {
    constexpr std::string_view kForbidden = "<>:\"/\\|?*";
    std::string result;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        result += (byte < 32 || kForbidden.find(c) != std::string_view::npos) ? '_' : c;
    }

    while (!result.empty() && (result.back() == '.' || result.back() == ' ')) result.pop_back();
    while (!result.empty() && result.front() == ' ') result.erase(result.begin());

    // Limite de 180 bytes, sem cortar um caractere UTF-8 no meio e preservando a extensão.
    constexpr size_t kMaxBytes = 180;
    if (result.size() > kMaxBytes) {
        const size_t dot = result.find_last_of('.');
        std::string extension = (dot != std::string::npos && result.size() - dot <= 16) ? result.substr(dot) : "";
        size_t cut = kMaxBytes - extension.size();
        while (cut > 0 && (static_cast<unsigned char>(result[cut]) & 0xC0) == 0x80) --cut;
        result = result.substr(0, cut) + extension;
    }

    const std::string stem = result.substr(0, result.find('.'));
    static constexpr std::array<std::string_view, 22> kReserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const auto reserved : kReserved) {
        if (equalsIgnoreCase(stem, reserved)) {
            result = "_" + result;
            break;
        }
    }
    return result.empty() ? "download" : result;
}

}  // namespace dm
