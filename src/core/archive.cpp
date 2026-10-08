#include "core/archive.h"

#include <algorithm>

namespace dm {

bool isArchiveExtension(const std::string& extension) {
    for (const char* known : {"zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "zst"}) {
        if (extension == known) return true;
    }
    return false;
}

std::vector<std::string> parseArchiveListing(const std::string& output, bool sevenZipSlt) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= output.size()) {
        size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        start = end + 1;
    }

    std::vector<std::string> paths;
    if (!sevenZipSlt) {
        for (const std::string& line : lines) {
            if (!line.empty()) paths.push_back(line);
        }
        return paths;
    }
    // O bloco antes de "----------" descreve o próprio compactado (Path = nome do arquivo).
    const auto separator = std::find(lines.begin(), lines.end(), "----------");
    for (auto it = separator == lines.end() ? lines.begin() : separator + 1; it != lines.end(); ++it) {
        if (it->rfind("Path = ", 0) == 0 && it->size() > 7) paths.push_back(it->substr(7));
    }
    return paths;
}

std::vector<std::string> topLevelNames(const std::vector<std::string>& paths) {
    std::vector<std::string> names;
    for (std::string path : paths) {
        while (path.rfind("./", 0) == 0 || path.rfind(".\\", 0) == 0) path.erase(0, 2);
        while (!path.empty() && (path.front() == '/' || path.front() == '\\')) path.erase(0, 1);
        const std::string first = path.substr(0, path.find_first_of("/\\"));
        if (first.empty() || first == "." || first == "..") continue;
        if (std::find(names.begin(), names.end(), first) == names.end()) names.push_back(first);
    }
    return names;
}

}  // namespace dm
