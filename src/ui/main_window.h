#pragma once

#include <windows.h>

#include <array>
#include <map>
#include <memory>
#include <string>

#include "app/download_manager.h"
#include "app/ipc.h"
#include "core/browser_request.h"
#include "core/settings.h"
#include "ui/download_list_view.h"
#include "ui/settings_page.h"
#include "ui/tray_icon.h"

namespace ui {

// Janela principal: barra de abas + botão "Adicionar" no topo e uma página por aba.
class MainWindow {
public:
    static constexpr const wchar_t* kClassName = app::kMainWindowClass;

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
    void applyQueueSettings();
    // "Quando todos os downloads terminarem": contagem regressiva e então suspende/desliga.
    void runWhenDoneAction();
    void checkWhenDone();
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
    // Pedido vindo da extensão do navegador (via dm-host.exe).
    void onBrowserRequest(const dm::BrowserRequest& request);
    void processBrowserRequests();
    LRESULT browserRequestStatus(const std::string& token);
    // Move para a pasta do app um arquivo que o navegador terminou. false: tentar de novo depois.
    bool adoptFile(const dm::AdoptRequest& request);
    void processAdoptions();
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
    bool sawWork_ = false;      // houve download desde que a ação de "quando terminar" foi escolhida
    bool keepingAwake_ = false;
    std::vector<dm::BrowserRequest> pendingBrowserRequests_;
    bool showingBrowserDialog_ = false;

    // Andamento dos pedidos do navegador, para a extensão saber quando largar a cópia dela.
    struct BrowserTicket {
        bool pending = true;
        bool declined = false;
        uint64_t id = 0;
    };
    std::map<std::string, BrowserTicket> browserTickets_;

    struct PendingAdoption {
        dm::AdoptRequest request;
        int attempts = 0;
    };
    std::vector<PendingAdoption> pendingAdoptions_;
};

}  // namespace ui
