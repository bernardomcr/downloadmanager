#include "core/update.h"

#include <cctype>

#include "core/json.h"

namespace dm {

std::optional<Version> parseVersion(const std::string& text) {
    size_t position = 0;
    if (position < text.size() && (text[position] == 'v' || text[position] == 'V')) ++position;
    Version version{};
    for (size_t part = 0; part < version.size(); ++part) {
        if (part > 0) {
            if (position >= text.size() || text[position] != '.') return std::nullopt;
            ++position;
        }
        const size_t start = position;
        int value = 0;
        while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position]))) {
            if (position - start >= 6) return std::nullopt;
            value = value * 10 + (text[position] - '0');
            ++position;
        }
        if (position == start) return std::nullopt;
        version[part] = value;
    }
    return position == text.size() ? std::optional<Version>(version) : std::nullopt;
}

std::string formatVersion(const Version& version) {
    return std::to_string(version[0]) + "." + std::to_string(version[1]) + "." + std::to_string(version[2]);
}

namespace {

bool isGitHubDownload(const std::string& url) {
    return url.rfind("https://github.com/", 0) == 0 || url.rfind("https://objects.githubusercontent.com/", 0) == 0;
}

}  // namespace

std::optional<ReleaseInfo> parseLatestRelease(const std::string& json, bool anyHost) {
    const auto root = parseJson(json);
    if (!root || !root->isObject()) return std::nullopt;
    if (root->boolean("draft").value_or(false) || root->boolean("prerelease").value_or(false)) return std::nullopt;
    const auto version = parseVersion(root->string("tag_name"));
    if (!version) return std::nullopt;

    ReleaseInfo info;
    info.version = *version;
    const JsonValue* assets = root->field("assets");
    if (!assets || !assets->array()) return std::nullopt;
    for (const JsonValue& asset : *assets->array()) {
        const std::string name = asset.string("name");
        const std::string url = asset.string("browser_download_url");
        if (!anyHost && !isGitHubDownload(url)) continue;
        if (name == kSetupAssetName) info.setupUrl = url;
        if (name == kChecksumAssetName) info.checksumUrl = url;
    }
    if (info.setupUrl.empty() || info.checksumUrl.empty()) return std::nullopt;
    return info;
}

std::string parseChecksumFile(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    if (text.size() - start < 64) return {};
    std::string hash;
    for (size_t i = start; i < start + 64; ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        hash += c;
    }
    if (start + 64 < text.size() && !std::isspace(static_cast<unsigned char>(text[start + 64]))) return {};
    return hash;
}

}  // namespace dm
