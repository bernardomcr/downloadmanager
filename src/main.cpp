#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include "ui/main_window.h"

namespace {

constexpr const wchar_t* kSingleInstanceMutex = L"Local\\DownloadManager.SingleInstance";

// Se o app já está aberto (por exemplo, só na bandeja), traz a janela existente para frente.
void activateRunningInstance() {
    HWND existing = FindWindowW(ui::MainWindow::kClassName, nullptr);
    if (!existing) return;
    ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(existing);
}

bool hasArgument(const wchar_t* wanted) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool found = false;
    for (int i = 1; i < count && !found; ++i) found = lstrcmpiW(arguments[i], wanted) == 0;
    LocalFree(arguments);
    return found;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        activateRunningInstance();
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS |
                                                        ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    // --tray: aberto pelo Windows ao entrar na sessão; começa só na bandeja.
    const bool startHidden = hasArgument(L"--tray");

    ui::MainWindow window;
    if (!window.create(instance, showCommand, startHidden)) return 1;

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (window.preTranslate(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
