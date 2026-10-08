#include "app/extractor.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cwctype>

#include "core/command_line.h"
#include "engine/process.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

std::wstring lower(std::wstring text) {
    for (wchar_t& c : text) c = static_cast<wchar_t>(std::towlower(c));
    return text;
}

std::wstring registryString(HKEY root, const wchar_t* key, const wchar_t* value) {
    for (const DWORD view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        wchar_t buffer[MAX_PATH * 2];
        DWORD size = sizeof(buffer);
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | view, nullptr, buffer, &size) == ERROR_SUCCESS) {
            return buffer;
        }
    }
    return {};
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

Extractor nanaZip() {
    // Instalado pela Microsoft Store: o atalho de linha de comando fica em WindowsApps.
    const std::wstring alias = dm::joinPath(knownFolder(FOLDERID_LocalAppData), L"Microsoft\\WindowsApps\\NanaZipG.exe");
    if (dm::fileExists(alias)) return {Extractor::Kind::SevenZip, alias, L"NanaZip"};
    return {};
}

Extractor sevenZip() {
    std::wstring folder = registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\7-Zip", L"Path");
    if (folder.empty()) folder = registryString(HKEY_CURRENT_USER, L"SOFTWARE\\7-Zip", L"Path");
    if (folder.empty()) folder = dm::joinPath(knownFolder(FOLDERID_ProgramFiles), L"7-Zip");
    const std::wstring exe = dm::joinPath(folder, L"7zG.exe");
    if (dm::fileExists(exe)) return {Extractor::Kind::SevenZip, exe, L"7-Zip"};
    return {};
}

Extractor winRar() {
    std::wstring exe = registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinRAR", L"exe64");
    if (exe.empty()) exe = registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WinRAR", L"exe32");
    if (exe.empty()) exe = dm::joinPath(knownFolder(FOLDERID_ProgramFiles), L"WinRAR\\WinRAR.exe");
    if (dm::fileExists(exe)) return {Extractor::Kind::WinRar, exe, L"WinRAR"};
    return {};
}

Extractor tar() {
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring exe = dm::joinPath(std::wstring(system, length), L"tar.exe");
    if (dm::fileExists(exe)) return {Extractor::Kind::Tar, exe, L"tar"};
    return {};
}

// Programa que abre esta extensão no Windows, se for um extrator conhecido.
Extractor associated(const std::wstring& extension) {
    wchar_t exe[MAX_PATH * 2];
    DWORD size = static_cast<DWORD>(std::size(exe));
    const std::wstring dotted = L"." + extension;
    if (FAILED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_EXECUTABLE, dotted.c_str(), nullptr, exe, &size))) {
        return {};
    }
    const std::wstring path = lower(exe);
    if (path.find(L"nanazip") != std::wstring::npos) return nanaZip();
    if (path.find(L"7z") != std::wstring::npos) {
        const std::wstring gui = dm::joinPath(dm::directoryOf(exe), L"7zG.exe");
        if (dm::fileExists(gui)) return {Extractor::Kind::SevenZip, gui, L"7-Zip"};
        return sevenZip();
    }
    if (path.find(L"winrar") != std::wstring::npos) {
        if (dm::fileExists(exe)) return {Extractor::Kind::WinRar, exe, L"WinRAR"};
        return winRar();
    }
    return {};
}

// Roda um programa com janela (a de progresso do extrator) e espera terminar. Devolve o código de saída.
int runVisible(const std::string& commandLine) {
    std::wstring command = dm::toWide(commandLine);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return -1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

}  // namespace

Extractor findExtractor(const std::wstring& extension) {
    for (const Extractor& candidate : {associated(lower(extension)), nanaZip(), sevenZip(), winRar(), tar()}) {
        if (candidate.kind != Extractor::Kind::None) return candidate;
    }
    return {};
}

bool extractArchive(const std::wstring& archive, const std::wstring& destination) {
    const std::wstring name = dm::fileNameOf(archive);
    const size_t dot = name.find_last_of(L'.');
    const Extractor extractor = findExtractor(dot == std::wstring::npos ? L"" : name.substr(dot + 1));
    if (extractor.kind == Extractor::Kind::None) return false;
    if (SHCreateDirectoryExW(nullptr, destination.c_str(), nullptr) != ERROR_SUCCESS &&
        GetLastError() != ERROR_ALREADY_EXISTS && !dm::fileExists(destination)) {
        return false;
    }

    const std::string exe = dm::toUtf8(extractor.exe);
    const std::string source = dm::toUtf8(archive);
    const std::string target = dm::toUtf8(destination);
    int code = -1;
    switch (extractor.kind) {
        case Extractor::Kind::SevenZip:
            // 7zG/NanaZipG: janela de progresso (e de senha, se precisar). Código 1 = só avisos.
            code = runVisible(dm::buildCommandLine(exe, {"x", source, "-o" + target, "-y"}));
            if (code == 1) code = 0;
            break;
        case Extractor::Kind::WinRar:
            // O destino do WinRAR termina com barra. Código 1 = só avisos.
            code = runVisible(dm::buildCommandLine(exe, {"x", "-y", source, target + "\\"}));
            if (code == 1) code = 0;
            break;
        case Extractor::Kind::Tar: {
            std::string output;
            code = dm::runAndCapture(dm::buildCommandLine(exe, {"-xf", source, "-C", target}), output);
            break;
        }
        default: break;
    }
    if (code != 0) RemoveDirectoryW(destination.c_str());  // só some se ficou vazia; o compactado é mantido
    return code == 0;
}

}  // namespace app
