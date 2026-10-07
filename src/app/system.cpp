#include "app/system.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <ctime>

namespace app {

std::wstring dataDirectory() {
    PWSTR base = nullptr;
    std::wstring directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &base))) {
        directory = std::wstring(base) + L"\\DownloadManager";
        CoTaskMemFree(base);
        CreateDirectoryW(directory.c_str(), nullptr);
    }
    return directory;
}

std::wstring defaultDownloadFolder() {
    PWSTR path = nullptr;
    std::wstring folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, nullptr, &path))) {
        folder = path;
        CoTaskMemFree(path);
    }
    return folder;
}

void setStartWithWindows(bool enabled) {
    constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t* kValue = L"DownloadManager";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (enabled) {
        wchar_t exe[MAX_PATH * 4];
        const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        const std::wstring command = L"\"" + std::wstring(exe, length) + L"\" --tray";
        RegSetValueExW(key, kValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kValue);
    }
    RegCloseKey(key);
}

void openFile(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void showInFolder(const std::wstring& path) {
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str())) {
        SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
        return;
    }
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) openFile(path.substr(0, slash));
}

bool moveToRecycleBin(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return true;
    std::wstring list;  // caminhos separados por \0, terminando com \0\0
    for (const auto& path : paths) list += path + L'\0';
    list += L'\0';
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = list.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}

void copyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
    }
    CloseClipboard();
}

std::wstring chooseFolder(HWND owner, const std::wstring& title, const std::wstring& initial) {
    std::wstring result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return result;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title.c_str());
    IShellItem* folder = nullptr;
    if (!initial.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        dialog->SetFolder(folder);
        folder->Release();
    }
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

int64_t unixNow() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::wstring formatDateTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return {};
    // Segundos Unix -> FILETIME (intervalos de 100 ns desde 1601).
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (static_cast<ULONGLONG>(unixSeconds) + 11644473600ULL) * 10000000ULL;
    FILETIME utc{ticks.LowPart, ticks.HighPart};
    FILETIME local;
    SYSTEMTIME time;
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &time);

    wchar_t date[64] = {};
    wchar_t clock[64] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, 64);
    return std::wstring(date) + L" " + clock;
}

}  // namespace app
