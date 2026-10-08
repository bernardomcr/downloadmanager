#include "core/rules.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dm {
namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string trim(const std::string& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(start, end - start);
}

std::string hostOf(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const size_t start = scheme + 3;
    std::string host = url.substr(start, url.find_first_of("/?#", start) - start);
    if (const size_t at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
    if (const size_t colon = host.find(':'); colon != std::string::npos) host = host.substr(0, colon);
    return lower(host);
}

bool siteMatches(const std::string& host, const std::string& site) {
    return host == site ||
           (host.size() > site.size() && host.ends_with(site) && host[host.size() - site.size() - 1] == '.');
}

const char* kindName(Rule::Kind kind) {
    switch (kind) {
        case Rule::Kind::File: return "file";
        case Rule::Kind::Video: return "video";
        default: return "any";
    }
}

Rule::Kind parseKind(const std::string& text) {
    if (text == "file") return Rule::Kind::File;
    if (text == "video") return Rule::Kind::Video;
    return Rule::Kind::Any;
}

std::string singleLine(const std::string& value) {
    std::string result;
    for (const char c : value) result += (c == '\r' || c == '\n') ? ' ' : c;
    return result;
}

Rule makeRule(const char* name, std::vector<std::string> extensions, Rule::Kind kind = Rule::Kind::Any) {
    Rule rule;
    rule.name = name;
    rule.folder = name;
    rule.extensions = std::move(extensions);
    rule.kind = kind;
    return rule;
}

}  // namespace

std::string fileExtension(const std::string& fileName) {
    const size_t slash = fileName.find_last_of("\\/");
    const std::string name = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return {};
    return lower(name.substr(dot + 1));
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> items;
    std::string current;
    auto flush = [&] {
        std::string item = lower(trim(current));
        while (!item.empty() && item.front() == '.') item.erase(item.begin());
        if (!item.empty() && std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
        current.clear();
    };
    for (const char c : text) {
        if (c == ',' || c == ';' || c == ' ') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return items;
}

std::string joinList(const std::vector<std::string>& items) {
    std::string text;
    for (const auto& item : items) {
        if (!text.empty()) text += ", ";
        text += item;
    }
    return text;
}

const Rule* matchRule(const std::vector<Rule>& rules, const DownloadFacts& facts) {
    const std::string extension = fileExtension(facts.fileName);
    const std::string host = hostOf(facts.url);
    const std::string name = lower(facts.fileName);

    for (const Rule& rule : rules) {
        if (!rule.enabled) continue;
        if (rule.kind == Rule::Kind::Video && !facts.isVideo) continue;
        if (rule.kind == Rule::Kind::File && facts.isVideo) continue;
        if (!rule.extensions.empty() &&
            std::find(rule.extensions.begin(), rule.extensions.end(), extension) == rule.extensions.end()) {
            continue;
        }
        if (!rule.sites.empty() && !std::any_of(rule.sites.begin(), rule.sites.end(),
                                                [&](const std::string& site) { return siteMatches(host, site); })) {
            continue;
        }
        if (!rule.nameContains.empty() && name.find(lower(rule.nameContains)) == std::string::npos) continue;
        if (rule.minSize > 0 && (facts.size < 0 || facts.size < rule.minSize)) continue;
        if (rule.maxSize > 0 && (facts.size < 0 || facts.size > rule.maxSize)) continue;
        return &rule;
    }
    return nullptr;
}

std::vector<Rule> defaultRules(bool portuguese) {
    std::vector<Rule> rules = {
        makeRule(portuguese ? "Vídeos" : "Videos", {}, Rule::Kind::Video),
        makeRule(portuguese ? "Compactados" : "Compressed",
                 {"zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "zst"}),
        makeRule(portuguese ? "Programas" : "Programs", {"exe", "msi", "msix", "msixbundle", "appx", "bat", "cmd", "apk"}),
        makeRule(portuguese ? "Imagens de disco" : "Disk images", {"iso", "img", "vhd", "vhdx"}),
        makeRule(portuguese ? "Documentos" : "Documents",
                 {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods", "odp", "txt", "rtf", "csv", "epub",
                  "md"}),
        makeRule(portuguese ? "Músicas" : "Music", {"mp3", "m4a", "aac", "flac", "wav", "ogg", "oga", "opus", "wma"}),
        makeRule(portuguese ? "Vídeos" : "Videos", {"mp4", "mkv", "avi", "mov", "wmv", "webm", "m4v", "flv", "ogv", "ts"}),
        makeRule(portuguese ? "Imagens" : "Pictures",
                 {"jpg", "jpeg", "png", "gif", "webp", "bmp", "svg", "heic", "avif", "tif", "tiff"}),
    };
    return rules;
}

std::string serializeRules(const std::vector<Rule>& rules) {
    std::ostringstream out;
    out << "dmrules 1\n";
    for (const Rule& rule : rules) {
        out << "\n[rule]\n";
        out << "name " << singleLine(rule.name) << '\n';
        out << "enabled " << (rule.enabled ? 1 : 0) << '\n';
        if (!rule.extensions.empty()) out << "extensions " << singleLine(joinList(rule.extensions)) << '\n';
        if (!rule.sites.empty()) out << "sites " << singleLine(joinList(rule.sites)) << '\n';
        if (!rule.nameContains.empty()) out << "name-contains " << singleLine(rule.nameContains) << '\n';
        if (rule.minSize > 0) out << "min-size " << rule.minSize << '\n';
        if (rule.maxSize > 0) out << "max-size " << rule.maxSize << '\n';
        out << "kind " << kindName(rule.kind) << '\n';
        out << "folder " << singleLine(rule.folder) << '\n';
        out << "extract " << (rule.extract ? 1 : 0) << '\n';
        out << "delete-archive " << (rule.deleteArchive ? 1 : 0) << '\n';
        out << "open-file " << (rule.openFile ? 1 : 0) << '\n';
        out << "open-folder " << (rule.openFolder ? 1 : 0) << '\n';
    }
    return out.str();
}

std::vector<Rule> parseRules(const std::string& text) {
    std::vector<Rule> rules;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return rules;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "dmrules 1") return rules;

    Rule current;
    bool inRule = false;
    auto flush = [&] {
        if (inRule && !current.folder.empty()) rules.push_back(current);
        current = Rule{};
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "[rule]") {
            flush();
            inRule = true;
            continue;
        }
        if (!inRule || line.empty()) continue;
        const size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string value = space == std::string::npos ? "" : line.substr(space + 1);
        auto number = [&] {
            std::istringstream parser(value);
            int64_t result = 0;
            parser >> result;
            return std::max<int64_t>(result, 0);
        };
        if (key == "name") current.name = value;
        else if (key == "enabled") current.enabled = value != "0";
        else if (key == "extensions") current.extensions = splitList(value);
        else if (key == "sites") current.sites = splitList(value);
        else if (key == "name-contains") current.nameContains = value;
        else if (key == "min-size") current.minSize = number();
        else if (key == "max-size") current.maxSize = number();
        else if (key == "kind") current.kind = parseKind(value);
        else if (key == "folder") current.folder = value;
        else if (key == "extract") current.extract = value == "1";
        else if (key == "delete-archive") current.deleteArchive = value == "1";
        else if (key == "open-file") current.openFile = value == "1";
        else if (key == "open-folder") current.openFolder = value == "1";
    }
    flush();
    return rules;
}

std::string resolveRuleFolder(const std::string& folder, const std::string& base) {
    const bool absolute = (folder.size() >= 2 && std::isalpha(static_cast<unsigned char>(folder[0])) && folder[1] == ':') ||
                          folder.rfind("\\\\", 0) == 0;
    if (absolute || base.empty()) return folder;
    if (folder.empty()) return base;
    const char last = base.back();
    return (last == '\\' || last == '/') ? base + folder : base + "\\" + folder;
}

std::string browserRouteFolder(const std::vector<Rule>& rules, const DownloadFacts& facts) {
    const Rule* rule = matchRule(rules, facts);
    if (!rule) return {};
    // Só pastas relativas: o navegador só aceita caminhos dentro da pasta de downloads dele.
    std::string folder = rule->folder;
    for (char& c : folder) {
        if (c == '\\') c = '/';
    }
    while (!folder.empty() && folder.back() == '/') folder.pop_back();
    const bool absolute = (folder.size() >= 2 && folder[1] == ':') || (!folder.empty() && folder.front() == '/');
    if (folder.empty() || absolute) return {};
    size_t start = 0;
    while (start <= folder.size()) {
        size_t end = folder.find('/', start);
        if (end == std::string::npos) end = folder.size();
        const std::string part = folder.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.find_first_of("<>:\"|?*") != std::string::npos) return {};
        start = end + 1;
    }
    return folder;
}

}  // namespace dm
