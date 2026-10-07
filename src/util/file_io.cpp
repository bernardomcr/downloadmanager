#include "util/file_io.h"

#include <windows.h>

namespace dm {

bool fileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::optional<std::string> readTextFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0) text.append(buffer, read);
    CloseHandle(file);
    return text;
}

bool writeTextFileAtomically(const std::wstring& path, const std::string& text) {
    const std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                    written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::wstring fileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring directoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}

std::wstring joinPath(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) return name;
    const wchar_t last = directory.back();
    return (last == L'\\' || last == L'/') ? directory + name : directory + L'\\' + name;
}

std::wstring numberedName(const std::wstring& name, int number) {
    if (number == 0) return name;
    const size_t dot = name.find_last_of(L'.');
    const std::wstring suffix = L" (" + std::to_wstring(number) + L")";
    if (dot == std::wstring::npos || dot == 0) return name + suffix;
    return name.substr(0, dot) + suffix + name.substr(dot);
}

std::wstring uniquePath(const std::wstring& directory, const std::wstring& name) {
    for (int number = 0;; ++number) {
        const std::wstring path = joinPath(directory, numberedName(name, number));
        if (!fileExists(path)) return path;
    }
}

}  // namespace dm
