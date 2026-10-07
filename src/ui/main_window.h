#pragma once

#include <windows.h>

#include <array>
#include <memory>
#include <string>

#include "app/download_manager.h"
#include "core/settings.h"
#include "ui/download_list_view.h"
#include "ui/settings_page.h"
#include "ui/tray_icon.h"

namespace ui {

// Janela principal: barra de abas + botão "Adicionar" no topo e uma página por aba.
class MainWindow {
public:
    static constexpr const wchar_t* kClassName = L"DownloadManager.MainWindow";

    // startHidden: aberto pelo Windows ao iniciar a sessão; fica só na bandeja.
    bool create(HINSTANCE instance, int showCommand, bool startHidden);
    // Teclado nos controles da aba Configurações (Tab, setas). Devolve true se a mensagem foi tratada.
    bool preTranslate(MSG& message);

private:
    enum Page { kDownloads, kCompleted, kRules, kSettings, kPageCount };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void loadSettings();
    void saveSettings();
    void onSettingsChanged(const dm::Settings& settings);
    void applyLanguage();
    void applyTexts();
    void updateTabTitles();

    void createControls();
    void applyDpi(UINT dpi);
    void layout();
    void showPage(int page);
    void refreshLists();
    void onTimer();
    void onAddClicked();
    void onTrayMessage(WPARAM wParam, LPARAM lParam);
    void showWindowFromTray();
    void exitApp();
    std::wstring downloadFolder() const;

    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND addButton_ = nullptr;
    HWND rulesList_ = nullptr;
    std::array<HWND, kPageCount> pages_{};
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    HICON iconLarge_ = nullptr;
    HICON iconSmall_ = nullptr;
    UINT dpi_ = 96;
    UINT taskbarCreatedMessage_ = 0;
    int currentPage_ = kDownloads;
    bool exiting_ = false;
    std::array<size_t, 2> tabCounts_{static_cast<size_t>(-1), static_cast<size_t>(-1)};

    std::wstring settingsPath_;
    dm::Settings settings_;
    std::unique_ptr<app::DownloadManager> manager_;
    DownloadListView downloadsList_;
    DownloadListView completedList_;
    SettingsPage settingsPage_;
    TrayIcon tray_;
    std::wstring lastCompletedPath_;
};

}  // namespace ui
