#pragma once

#include <windows.h>

#include <array>
#include <map>
#include <memory>
#include <string>

#include "app/download_manager.h"
#include "app/ipc.h"
#include "app/updater.h"
#include "app/video_tools.h"
#include "core/browser_request.h"
#include "core/settings.h"
#include <shellapi.h>

#include "ui/dialogs.h"
#include "ui/download_list_view.h"
#include "ui/rules_page.h"
#include "ui/settings_page.h"
#include "ui/tray_icon.h"

namespace ui {

// Janela principal: barra de abas + botão "Adicionar" no topo e uma página por aba.
class MainWindow {
public:
    static constexpr const wchar_t* kClassName = app::kMainWindowClass;

    // startHidden: aberto pelo Windows ao iniciar a sessão; fica só na bandeja.
    // false também quando abriu o instalador de uma atualização (aí exitingForUpdate() é true).
    bool create(HINSTANCE instance, int showCommand, bool startHidden);
    bool exitingForUpdate() const { return updateLaunched_; }
    // Teclado nos controles da aba Configurações (Tab, setas). Devolve true se a mensagem foi tratada.
    bool preTranslate(MSG& message);

private:
    enum Page { kDownloads, kCompleted, kRules, kSettings, kPageCount };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void loadSettings();
    void saveSettings();
    void loadRules();
    void saveRules();
    void applyRules();
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
    // Valores comuns do diálogo Adicionar (pasta padrão e regras para prever a pasta final).
    void prepareAddRequest(AddRequest& request) const;
    // Magnet ou .torrent pelo Real-Debrid. Sem conta conectada, avisa e abre Configurações. 0 = não adicionou.
    uint64_t addTorrent(std::wstring input, const std::wstring& folder, bool organize);
    void onDropFiles(HDROP drop);
    // Real-Debrid: token para as tarefas e conferência da conta em segundo plano.
    void applyDebridToken();
    void checkDebridAccount(const std::string& token, bool connecting);
    void onDebridChecked(LPARAM result);
    // Fluxo de vídeo (análise + escolhas). Devolve o id do primeiro download criado, 0 se cancelado.
    uint64_t addVideoFlow(const std::wstring& url, const std::wstring& folder, const std::wstring& title,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& userAgent, bool& declined);
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
    // Atualização automática: procura, mostra o estado em Configurações e instala na hora certa.
    void updateTick();
    void refreshUpdateStatus();
    bool installUpdate(const wchar_t* relaunch);
    std::wstring downloadFolder() const;
    // Download novo: marca para as regras organizarem se foi para a pasta padrão.
    void markOrganize(uint64_t id, const std::wstring& folder);

    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND addButton_ = nullptr;
    std::array<HWND, kPageCount> pages_{};
    HFONT font_ = nullptr;
    HWND tabs_ = nullptr;
    HFONT listFont_ = nullptr;
    // Rodapé de status (desenhado em WM_PAINT).
    int footerHeight_ = 0;
    std::wstring footerText_;
    std::wstring footerSpeed_;
    void paint(HDC dc);
    void updateFooter();
    HBRUSH background_ = nullptr;
    HICON iconLarge_ = nullptr;
    HICON iconSmall_ = nullptr;
    UINT dpi_ = 96;
    UINT taskbarCreatedMessage_ = 0;
    int currentPage_ = kDownloads;
    bool exiting_ = false;
    std::array<size_t, 2> tabCounts_{static_cast<size_t>(-1), static_cast<size_t>(-1)};

    std::wstring settingsPath_;
    std::wstring rulesPath_;
    std::vector<dm::Rule> rules_;
    dm::Settings settings_;
    std::unique_ptr<app::DownloadManager> manager_;
    std::unique_ptr<app::VideoTools> videoTools_;
    std::unique_ptr<app::Updater> updater_;
    bool installedCopy_ = false;
    bool updateLaunched_ = false;
    bool quitByInstaller_ = false;
    int updateTicks_ = 0;
    int lastUpdateState_ = -1;
    DownloadListView downloadsList_;
    DownloadListView completedList_;
    SettingsPage settingsPage_;
    RulesPage rulesPage_;
    TrayIcon tray_;
    uint64_t lastCompletedId_ = 0;  // clique na notificação abre a pasta dele (o caminho muda se a regra mover)
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
    std::vector<dm::DownloadRecord> pendingWebPages_;
};

}  // namespace ui
