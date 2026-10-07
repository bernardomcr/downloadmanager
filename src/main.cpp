#include <windows.h>
#include <commctrl.h>

#include "i18n/strings.h"
#include "ui/main_window.h"

namespace {

constexpr const wchar_t* kSingleInstanceMutex = L"Local\\DownloadManager.SingleInstance";

// Se o app já está aberto (por exemplo, minimizado na bandeja), traz a janela existente para frente.
bool activateRunningInstance() {
    HWND existing = FindWindowW(ui::MainWindow::kClassName, nullptr);
    if (!existing) return false;
    ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(existing);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        activateRunningInstance();
        return 0;
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
    InitCommonControlsEx(&controls);

    i18n::setLanguage(i18n::systemLanguage());

    ui::MainWindow window;
    if (!window.create(instance, showCommand)) return 1;

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
