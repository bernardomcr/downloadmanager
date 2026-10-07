#include "ui/tray_icon.h"

#include <shellapi.h>

namespace ui {
namespace {

void copyText(wchar_t* target, size_t capacity, const std::wstring& text) {
    lstrcpynW(target, text.c_str(), static_cast<int>(capacity));
}

}  // namespace

void TrayIcon::add(HWND owner, HICON icon, const std::wstring& tooltip) {
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    data_.uCallbackMessage = kCallbackMessage;
    data_.hIcon = icon;
    copyText(data_.szTip, std::size(data_.szTip), tooltip);
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
}

void TrayIcon::remove() {
    if (added_) Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false;
}

void TrayIcon::recreate() {
    if (!data_.hWnd) return;
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) Shell_NotifyIconW(NIM_SETVERSION, &data_);
}

void TrayIcon::setTooltip(const std::wstring& tooltip) {
    if (!added_) return;
    data_.uFlags = NIF_TIP | NIF_SHOWTIP;
    copyText(data_.szTip, std::size(data_.szTip), tooltip);
    Shell_NotifyIconW(NIM_MODIFY, &data_);
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
}

void TrayIcon::showNotification(const std::wstring& title, const std::wstring& text) {
    if (!added_) return;
    NOTIFYICONDATAW balloon = data_;
    balloon.uFlags = NIF_INFO;
    balloon.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    balloon.hBalloonIcon = data_.hIcon;
    copyText(balloon.szInfoTitle, std::size(balloon.szInfoTitle), title);
    copyText(balloon.szInfo, std::size(balloon.szInfo), text);
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

}  // namespace ui
