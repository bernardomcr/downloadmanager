#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace ui {

// Ícone na área de notificação. Mensagens chegam à janela dona como `callbackMessage`.
class TrayIcon {
public:
    static constexpr UINT kCallbackMessage = WM_APP + 1;

    void add(HWND owner, HICON icon, const std::wstring& tooltip);
    void remove();
    // O Explorer reiniciou: o ícone precisa ser recriado.
    void recreate();
    void setTooltip(const std::wstring& tooltip);
    void showNotification(const std::wstring& title, const std::wstring& text);

private:
    NOTIFYICONDATAW data_{};
    bool added_ = false;
};

}  // namespace ui
