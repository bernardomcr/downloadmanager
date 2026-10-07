// DownloadManager-Setup.exe: instala o Download Manager só para o usuário atual (sem pedir
// administrador, o que também deixa a atualização automática silenciosa) e desinstala.
//   DownloadManager-Setup.exe                         janela: "Iniciar com o Windows" (marcado) e atalho
//   DownloadManager-Setup.exe /silencioso [/abrir | /bandeja]   atualização automática
//   uninstall.exe /desinstalar [/silencioso]          Apps e recursos do Windows
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <string>
#include <thread>
#include <vector>

#include "app/browser_integration.h"
#include "app/ipc.h"
#include "app/system.h"
#include "core/settings.h"
#include "i18n/strings.h"
#include "payload.h"
#include "resource.h"
#include "util/file_io.h"
#include "util/unicode.h"
#include "version.h"

using i18n::Str;
using i18n::tr;

namespace {

constexpr const wchar_t* kProductName = L"Download Manager";
constexpr const wchar_t* kAppExe = L"DownloadManager.exe";
constexpr const wchar_t* kUninstallerExe = L"uninstall.exe";
constexpr const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DownloadManager";
constexpr const wchar_t* kFirefoxXpi = L"download-manager.xpi";
constexpr UINT kInstallFinished = WM_APP + 1;

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

// %LOCALAPPDATA%\Programs\Download Manager
std::wstring installDirectory() {
    std::wstring programs = knownFolder(FOLDERID_UserProgramFiles);
    if (programs.empty()) programs = dm::joinPath(knownFolder(FOLDERID_LocalAppData), L"Programs");
    return dm::joinPath(programs, kProductName);
}

std::wstring startMenuShortcut() {
    return dm::joinPath(knownFolder(FOLDERID_Programs), std::wstring(kProductName) + L".lnk");
}

std::wstring desktopShortcut() {
    return dm::joinPath(knownFolder(FOLDERID_Desktop), std::wstring(kProductName) + L".lnk");
}

std::wstring ownPath() {
    wchar_t path[MAX_PATH * 4];
    return std::wstring(path, GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))));
}

std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : L"";
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return text + L" (" + std::to_wstring(code) + L")";
}

bool hasArgument(const wchar_t* wanted) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool found = false;
    for (int i = 1; i < count && !found; ++i) found = lstrcmpiW(arguments[i], wanted) == 0;
    LocalFree(arguments);
    return found;
}

// Valor depois de um argumento ("/desinstalar <pasta>"); vazio se não houver.
std::wstring argumentAfter(const wchar_t* wanted) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring value;
    for (int i = 1; i + 1 < count; ++i) {
        if (lstrcmpiW(arguments[i], wanted) == 0 && arguments[i + 1][0] != L'/') value = arguments[i + 1];
    }
    LocalFree(arguments);
    return value;
}

// Pede para o app aberto salvar tudo e fechar; se não fechar em 20 s, encerra (o estado dos downloads
// aguenta travamento). Devolve false se continuar aberto.
bool closeRunningApp() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        HWND window = FindWindowW(app::kMainWindowClass, nullptr);
        if (!window) return true;
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, processId);
        PostMessageW(window, app::kMessageQuit, 0, 0);
        if (process) {
            if (WaitForSingleObject(process, 20000) == WAIT_TIMEOUT) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 5000);
            }
            CloseHandle(process);
        } else {
            Sleep(1000);
        }
    }
    if (FindWindowW(app::kMainWindowClass, nullptr)) return false;
    // A janela some antes do processo terminar de salvar: espera a instância única ser liberada,
    // senão o app reaberto achava que já havia um aberto.
    for (int attempt = 0; attempt < 120; ++attempt) {
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\DownloadManager.SingleInstance");
        if (!mutex) break;
        CloseHandle(mutex);
        Sleep(250);
    }
    return true;
}

void removeTree(const std::wstring& path) {
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW(dm::joinPath(path, L"*").c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = entry.cFileName;
            if (name == L"." || name == L"..") continue;
            const std::wstring child = dm::joinPath(path, name);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeTree(child);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    RemoveDirectoryW(path.c_str());
}

// Sobras de atualizações anteriores (arquivos que estavam em uso e foram renomeados).
void removeOldFiles(const std::wstring& directory) {
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW(dm::joinPath(directory, L"*.old").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        DeleteFileW(dm::joinPath(directory, entry.cFileName).c_str());
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

// Grava ao lado e troca. Um .exe em uso (o dm-host.exe aberto pelo navegador) não pode ser
// sobrescrito, mas pode ser renomeado: o antigo vira .old e é apagado na próxima vez.
bool installFile(const std::wstring& target, const void* data, DWORD size, DWORD& error) {
    SHCreateDirectoryExW(nullptr, dm::directoryOf(target).c_str(), nullptr);
    const std::wstring fresh = target + L".new";
    HANDLE file = CreateFileW(fresh.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, data, size, &written, nullptr) && written == size && FlushFileBuffers(file);
    error = GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(fresh.c_str());
        return false;
    }
    if (MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    const std::wstring old = target + L".old";
    if (MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) &&
        MoveFileExW(fresh.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(old.c_str());
        return true;
    }
    error = GetLastError();
    DeleteFileW(fresh.c_str());
    return false;
}

bool createShortcut(const std::wstring& shortcut, const std::wstring& target) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(dm::directoryOf(target).c_str());
    link->SetIconLocation(target.c_str(), 0);
    link->SetDescription(kProductName);
    IPersistFile* file = nullptr;
    bool ok = false;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
        ok = SUCCEEDED(file->Save(shortcut.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

void setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void setNumber(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// Entrada em "Apps e recursos" (HKCU: instalação só deste usuário).
void registerUninstaller(const std::wstring& directory, DWORD sizeKb) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    const std::wstring uninstaller = dm::joinPath(directory, kUninstallerExe);
    setString(key, L"DisplayName", kProductName);
    setString(key, L"DisplayVersion", dm::toWide(DM_VERSION_STRING));
    setString(key, L"Publisher", L"bernardomcr");
    setString(key, L"DisplayIcon", dm::joinPath(directory, kAppExe));
    setString(key, L"InstallLocation", directory);
    setString(key, L"UninstallString", L"\"" + uninstaller + L"\" /desinstalar");
    setString(key, L"QuietUninstallString", L"\"" + uninstaller + L"\" /desinstalar /silencioso");
    setString(key, L"URLInfoAbout", L"https://github.com/bernardomcr/downloadmanager");
    setNumber(key, L"EstimatedSize", sizeKb);
    setNumber(key, L"NoModify", 1);
    setNumber(key, L"NoRepair", 1);
    RegCloseKey(key);
}

// Firefox oferece instalar a extensão (assinada pela Mozilla) na próxima vez que abrir.
void registerFirefoxExtension(const std::wstring& xpi) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Mozilla\\Firefox\\Extensions", 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    setString(key, app::kFirefoxExtensionId, xpi);
    RegCloseKey(key);
}

// A escolha do instalador vira a configuração do app (que mantém o registro em dia ao abrir).
void saveStartupChoice(bool startWithWindows, const std::wstring& appPath) {
    const std::wstring settingsPath = dm::joinPath(app::dataDirectory(), L"settings.ini");
    dm::Settings settings;
    if (const auto text = dm::readTextFile(settingsPath)) settings = dm::parseSettings(*text);
    settings.startWithWindows = startWithWindows;
    dm::writeTextFileAtomically(settingsPath, dm::serializeSettings(settings));
    app::setStartWithWindows(startWithWindows, appPath);
}

struct InstallOptions {
    bool interactive = true;
    bool startWithWindows = true;
    bool desktopShortcut = true;
};

// Copia tudo e registra. Vazio se deu certo; senão, o motivo.
std::wstring install(const InstallOptions& options) {
    const std::wstring directory = installDirectory();
    if (!closeRunningApp()) return errorText(ERROR_SHARING_VIOLATION);
    SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    removeOldFiles(directory);
    removeTree(dm::joinPath(directory, L"extension"));  // arquivos que saíram da extensão não ficam para trás

    HMODULE module = GetModuleHandleW(nullptr);
    DWORD totalSize = 0;
    bool hasXpi = false;
    for (const auto& file : kPayload) {
        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(file.id), MAKEINTRESOURCEW(10));  // RT_RCDATA
        HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
        const void* data = loaded ? LockResource(loaded) : nullptr;
        if (!data) return errorText(ERROR_RESOURCE_DATA_NOT_FOUND);
        const DWORD size = SizeofResource(module, resource);
        std::wstring target = directory;
        for (const wchar_t* part = file.path; *part;) {  // caminhos do pacote usam "/"
            const wchar_t* end = wcschr(part, L'/');
            const std::wstring piece = end ? std::wstring(part, end) : std::wstring(part);
            target = dm::joinPath(target, piece);
            part = end ? end + 1 : part + wcslen(part);
        }
        DWORD error = 0;
        if (!installFile(target, data, size, error)) return errorText(error);
        totalSize += size;
        if (lstrcmpiW(file.path, kFirefoxXpi) == 0) hasXpi = true;
    }

    // O desinstalador é este mesmo programa.
    {
        const std::wstring self = ownPath();
        const auto bytes = dm::readTextFile(self);
        DWORD error = ERROR_READ_FAULT;
        if (!bytes || !installFile(dm::joinPath(directory, kUninstallerExe), bytes->data(),
                                   static_cast<DWORD>(bytes->size()), error)) {
            return errorText(error);
        }
        totalSize += static_cast<DWORD>(bytes->size());
    }

    const std::wstring appPath = dm::joinPath(directory, kAppExe);
    createShortcut(startMenuShortcut(), appPath);
    if (options.interactive) {
        if (options.desktopShortcut) {
            createShortcut(desktopShortcut(), appPath);
        } else {
            DeleteFileW(desktopShortcut().c_str());
        }
        saveStartupChoice(options.startWithWindows, appPath);
    } else if (dm::fileExists(desktopShortcut())) {
        createShortcut(desktopShortcut(), appPath);  // atualização: só mantém o que já existia
    }
    registerUninstaller(directory, totalSize / 1024);
    if (hasXpi) registerFirefoxExtension(dm::joinPath(directory, kFirefoxXpi));
    return {};
}

void launchApp(const wchar_t* arguments) {
    const std::wstring appPath = dm::joinPath(installDirectory(), kAppExe);
    ShellExecuteW(nullptr, L"open", appPath.c_str(), arguments, installDirectory().c_str(), SW_SHOWNORMAL);
}

// --- Janela do instalador ---

struct SetupWindow {
    HFONT titleFont = nullptr;
    HBRUSH white = nullptr;
    InstallOptions options;
    std::wstring result;
    bool installing = false;
};

INT_PTR CALLBACK setupProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(dialog, DWLP_USER));
    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<SetupWindow*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            state->white = CreateSolidBrush(RGB(255, 255, 255));
            SetWindowTextW(dialog, tr(Str::SetupTitle));
            const std::wstring title = std::wstring(kProductName) + L" " + dm::toWide(DM_VERSION_STRING);
            SetDlgItemTextW(dialog, IDC_SETUP_TITLE, title.c_str());
            SetDlgItemTextW(dialog, IDC_SETUP_FOLDER_LABEL, tr(Str::SetupFolder));
            SetDlgItemTextW(dialog, IDC_SETUP_FOLDER, installDirectory().c_str());
            SetDlgItemTextW(dialog, IDC_SETUP_STARTUP, tr(Str::SetupStartWithWindows));
            SetDlgItemTextW(dialog, IDC_SETUP_DESKTOP, tr(Str::SetupDesktopShortcut));
            SetDlgItemTextW(dialog, IDOK, tr(Str::SetupInstall));
            SetDlgItemTextW(dialog, IDCANCEL, tr(Str::Cancel));
            CheckDlgButton(dialog, IDC_SETUP_STARTUP, BST_CHECKED);
            CheckDlgButton(dialog, IDC_SETUP_DESKTOP, BST_CHECKED);
            ShowWindow(GetDlgItem(dialog, IDC_SETUP_PROGRESS), SW_HIDE);

            HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
            LOGFONTW logFont{};
            GetObjectW(font, sizeof(logFont), &logFont);
            logFont.lfHeight = logFont.lfHeight * 3 / 2;
            logFont.lfWeight = FW_SEMIBOLD;
            state->titleFont = CreateFontIndirectW(&logFont);
            SendDlgItemMessageW(dialog, IDC_SETUP_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(state->titleFont), TRUE);

            const HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP),
                                                             IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
            SendMessageW(dialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
            return TRUE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
            return reinterpret_cast<INT_PTR>(state->white);
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && !state->installing) {
                state->installing = true;
                state->options.startWithWindows = IsDlgButtonChecked(dialog, IDC_SETUP_STARTUP) == BST_CHECKED;
                state->options.desktopShortcut = IsDlgButtonChecked(dialog, IDC_SETUP_DESKTOP) == BST_CHECKED;
                for (int id : {IDOK, IDCANCEL, IDC_SETUP_STARTUP, IDC_SETUP_DESKTOP}) {
                    EnableWindow(GetDlgItem(dialog, id), FALSE);
                }
                SetDlgItemTextW(dialog, IDOK, tr(Str::SetupInstalling));
                HWND progress = GetDlgItem(dialog, IDC_SETUP_PROGRESS);
                ShowWindow(progress, SW_SHOW);
                SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
                std::thread([dialog, state] {
                    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                    state->result = install(state->options);
                    CoUninitialize();
                    PostMessageW(dialog, kInstallFinished, 0, 0);
                }).detach();
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL && !state->installing) {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        case kInstallFinished:
            if (!state->result.empty()) {
                wchar_t text[1024];
                std::swprintf(text, 1024, tr(Str::SetupFailed), state->result.c_str());
                MessageBoxW(dialog, text, tr(Str::SetupTitle), MB_OK | MB_ICONERROR);
                EndDialog(dialog, IDABORT);
            } else {
                EndDialog(dialog, IDOK);
            }
            return TRUE;
        case WM_CLOSE:
            if (!state->installing) EndDialog(dialog, IDCANCEL);
            return TRUE;
        case WM_DESTROY:
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->white) DeleteObject(state->white);
            return FALSE;
    }
    return FALSE;
}

int runSetup() {
    if (hasArgument(L"/silencioso")) {
        InstallOptions options;
        options.interactive = false;
        const std::wstring error = install(options);
        if (!error.empty()) return 1;
        if (hasArgument(L"/abrir")) launchApp(nullptr);
        if (hasArgument(L"/bandeja")) launchApp(L"--tray");
        return 0;
    }
    SetupWindow state;
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETUP), nullptr, setupProc,
                                           reinterpret_cast<LPARAM>(&state));
    if (result != IDOK) return result == IDCANCEL ? 0 : 1;
    launchApp(nullptr);
    return 0;
}

// --- Desinstalação ---

// O uninstall.exe não pode apagar a si mesmo nem a pasta onde está: copia-se para a pasta
// temporária e continua de lá.
int relaunchFromTemp() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring copy = dm::joinPath(temp, L"dm-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    if (!CopyFileW(ownPath().c_str(), copy.c_str(), FALSE)) return 1;
    std::wstring command = L"\"" + copy + L"\" /desinstalar \"" + installDirectory() + L"\"";
    if (hasArgument(L"/silencioso")) command += L" /silencioso";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return 1;
    }
    // Sai logo: a cópia precisa apagar este arquivo.
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

// Pergunta com a opção de apagar também os dados do app. false: cancelado.
bool confirmUninstall(bool& deleteData) {
    TASKDIALOG_BUTTON buttons[] = {{IDOK, tr(Str::UninstallButton)}, {IDCANCEL, tr(Str::Cancel)}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hInstance = GetModuleHandleW(nullptr);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = tr(Str::UninstallTitle);
    config.pszMainIcon = MAKEINTRESOURCEW(IDI_APP);
    config.pszMainInstruction = tr(Str::UninstallQuestion);
    config.pszContent = tr(Str::UninstallDetail);
    config.pszVerificationText = tr(Str::UninstallDeleteData);
    config.pButtons = buttons;
    config.cButtons = 2;
    config.nDefaultButton = IDOK;
    int button = IDCANCEL;
    BOOL verification = FALSE;
    if (FAILED(TaskDialogIndirect(&config, &button, nullptr, &verification))) return false;
    deleteData = verification != FALSE;
    return button == IDOK;
}

void scheduleSelfDelete() {
    std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 > nul & del /f /q \"" + ownPath() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

int runUninstall() {
    const std::wstring directory = argumentAfter(L"/desinstalar");
    if (directory.empty()) return relaunchFromTemp();

    const bool silent = hasArgument(L"/silencioso");
    bool deleteData = false;
    if (!silent && !confirmUninstall(deleteData)) {
        scheduleSelfDelete();
        return 0;
    }
    closeRunningApp();
    app::setStartWithWindows(false);
    app::unregisterBrowserIntegration();
    DeleteFileW(startMenuShortcut().c_str());
    DeleteFileW(desktopShortcut().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    // O uninstall.exe original pode levar um instante para fechar.
    for (int attempt = 0; attempt < 20 && dm::fileExists(directory); ++attempt) {
        removeTree(directory);
        if (dm::fileExists(directory)) Sleep(250);
    }
    if (deleteData) removeTree(app::dataDirectory());
    if (!silent) MessageBoxW(nullptr, tr(Str::UninstallDone), tr(Str::UninstallTitle), MB_OK | MB_ICONINFORMATION);
    scheduleSelfDelete();
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    i18n::setLanguage(i18n::systemLanguage());

    const int code = hasArgument(L"/desinstalar") ? runUninstall() : runSetup();
    CoUninitialize();
    return code;
}
