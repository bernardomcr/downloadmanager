#include "app/firefox_prefs.h"

#include <windows.h>
#include <shlobj.h>

#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

constexpr const char* kBegin = "// Download Manager: início (pasta dos downloads do navegador)";
constexpr const char* kEnd = "// Download Manager: fim";

std::wstring roamingAppData() {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

std::string jsString(const std::string& text) {
    std::string escaped;
    for (const char c : text) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    return escaped;
}

}  // namespace

std::string firefoxPrefsBlock(const std::wstring& folder) {
    std::string block = std::string(kBegin) + "\n";
    if (folder.empty()) {
        block += "user_pref(\"browser.download.folderList\", 1);\n";  // pasta Downloads padrão
    } else {
        block += "user_pref(\"browser.download.folderList\", 2);\n";
        block += "user_pref(\"browser.download.dir\", \"" + jsString(dm::toUtf8(folder)) + "\");\n";
    }
    return block + kEnd + "\n";
}

std::string replaceFirefoxPrefsBlock(const std::string& userJs, const std::string& block) {
    const size_t begin = userJs.find(kBegin);
    if (begin != std::string::npos) {
        size_t end = userJs.find(kEnd, begin);
        if (end != std::string::npos) {
            end += std::string(kEnd).size();
            if (end < userJs.size() && userJs[end] == '\r') ++end;
            if (end < userJs.size() && userJs[end] == '\n') ++end;
            return userJs.substr(0, begin) + block + userJs.substr(end);
        }
    }
    std::string result = userJs;
    if (!result.empty() && result.back() != '\n') result += '\n';
    return result + block;
}

void setFirefoxDownloadFolder(const std::wstring& folder) {
    const std::wstring profiles = dm::joinPath(roamingAppData(), L"Mozilla\\Firefox\\Profiles");
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(dm::joinPath(profiles, L"*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return;  // sem Firefox
    const std::string block = firefoxPrefsBlock(folder);
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == L'.') continue;
        const std::wstring profile = dm::joinPath(profiles, entry.cFileName);
        if (!dm::fileExists(dm::joinPath(profile, L"prefs.js"))) continue;  // não é um perfil usado
        const std::wstring userJs = dm::joinPath(profile, L"user.js");
        const std::string current = dm::readTextFile(userJs).value_or("");
        const std::string updated = replaceFirefoxPrefsBlock(current, block);
        if (updated != current) dm::writeTextFileAtomically(userJs, updated);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

}  // namespace app
