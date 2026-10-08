#include "ui/main_window.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <thread>

#include "app/browser_integration.h"
#include "app/debrid_account.h"
#include "core/debrid.h"
#include "i18n/errors.h"
#include "util/secure.h"
#include "app/system.h"
#include "i18n/strings.h"
#include "resource.h"
#include "core/video.h"
#include "version.h"
#include "ui/dialogs.h"
#include "ui/theme.h"
#include "ui/video_dialog.h"
#include "core/format.h"
#include "core/http_headers.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

// Medidas em pixels lógicos (96 DPI); convertidas com scale().
constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 620;
constexpr int kMinWidth = 640;
constexpr int kMinHeight = 420;
constexpr int kMargin = 8;
constexpr int kFooterHeight = 30;    // velocidade total e contagem
constexpr int kAddButtonWidth = 104;
constexpr COLORREF kBackground = theme::kBackground;

constexpr int kIdTabs = 100;
constexpr int kIdAddButton = 101;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kProcessBrowserRequests = WM_APP + 2;
constexpr UINT kRefreshMilliseconds = 500;
constexpr UINT kDebridChecked = WM_APP + 3;  // lParam: DebridCheckResult* (dono: quem recebe)

struct DebridCheckResult {
    app::DebridAccountCheck check;
    std::string token;
    bool connecting = false;  // true: o usuário clicou em Conectar (salva se der certo)
};

enum TrayCommand { kTrayOpen = 3001, kTrayExit };

// Mesma pasta, ignorando maiúsculas e barra no fim.
bool sameFolder(std::wstring a, std::wstring b) {
    while (!a.empty() && (a.back() == L'\\' || a.back() == L'/')) a.pop_back();
    while (!b.empty() && (b.back() == L'\\' || b.back() == L'/')) b.pop_back();
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace

bool MainWindow::create(HINSTANCE instance, int showCommand, bool startHidden) {
    instance_ = instance;
    background_ = CreateSolidBrush(kBackground);
    iconLarge_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    const std::wstring dataDirectory = app::dataDirectory();
    settingsPath_ = dm::joinPath(dataDirectory, L"settings.ini");
    rulesPath_ = dm::joinPath(dataDirectory, L"rules.ini");
    if (!app::testProfile()) app::registerBrowserIntegration(dataDirectory);
    loadSettings();
    applyLanguage();
    updater_ = std::make_unique<app::Updater>(dataDirectory);
    installedCopy_ = app::runningFromInstallation();
    // Versão nova baixada antes: instala antes de abrir (uma tentativa só, se o instalador falhar).
    if (settings_.autoUpdate && installedCopy_ && updater_->ready() && !updater_->alreadyAttempted() &&
        installUpdate(startHidden ? L"/bandeja" : L"/abrir")) {
        return false;
    }
    manager_ = std::make_unique<app::DownloadManager>(dm::joinPath(dataDirectory, L"downloads.dat"));
    videoTools_ = std::make_unique<app::VideoTools>(dataDirectory);
    manager_->setVideoTools(videoTools_.get());
    loadRules();
    applyDebridToken();  // antes de load(): torrents que estavam no Real-Debrid continuam
    videoTools_->updateIfStale();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background_;
    windowClass.lpszClassName = kClassName;
    windowClass.hIcon = iconLarge_;
    windowClass.hIconSm = iconSmall_;
    if (!RegisterClassExW(&windowClass)) return false;

    hwnd_ = CreateWindowExW(0, kClassName, tr(Str::AppTitle), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                            CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    theme::applyWindowFrame(hwnd_);

    // A janela foi criada no DPI do monitor; ajusta o tamanho inicial para ele.
    SetWindowPos(hwnd_, nullptr, 0, 0, scale(kWindowWidth), scale(kWindowHeight),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!startHidden) {
        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
    }
    return true;
}

bool MainWindow::preTranslate(MSG& message) {
    // Atalhos: Ctrl+Tab troca de aba, Ctrl+N adiciona.
    if (message.message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0 &&
        (message.hwnd == hwnd_ || IsChild(hwnd_, message.hwnd))) {
        if (message.wParam == VK_TAB) {
            const int step = GetKeyState(VK_SHIFT) < 0 ? kPageCount - 1 : 1;
            showPage((currentPage_ + step) % kPageCount);
            return true;
        }
        if (message.wParam == 'N') {
            onAddClicked();
            return true;
        }
    }
    HWND page = currentPage_ == kSettings ? settingsPage_.handle() : currentPage_ == kRules ? rulesPage_.handle() : nullptr;
    return page && IsChild(page, message.hwnd) && IsDialogMessageW(page, &message);
}

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        tray_.recreate();
        return 0;
    }

    switch (message) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(hwnd_);
            taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
            createControls();
            applyDpi(dpi_);
            manager_->onCompleted = [this](const app::DownloadItem& item) {
                lastCompletedId_ = item.record.id;
                if (settings_.notifyOnComplete) {
                    tray_.showNotification(tr(Str::NotifyCompleted),
                                           dm::fileNameOf(dm::toWide(item.record.filePath)));
                }
            };
            manager_->onOrganized = [](const std::wstring& path, bool openFile, bool openFolder) {
                if (openFile) app::openFile(path);
                if (openFolder) app::showInFolder(path);
            };
            applyQueueSettings();
            manager_->onWebPage = [this](const dm::DownloadRecord& record) {
                // Chega pelo timer; o diálogo abre depois, fora do tick.
                pendingWebPages_.push_back(record);
                PostMessageW(hwnd_, kProcessBrowserRequests, 0, 0);
            };
            manager_->load();
            DragAcceptFiles(hwnd_, TRUE);  // arrastar .torrent para a janela
            if (!settings_.realDebridToken.empty()) {
                if (const auto token = dm::unprotectForCurrentUser(settings_.realDebridToken)) {
                    checkDebridAccount(*token, false);
                }
            }
            tray_.add(hwnd_, iconSmall_, tr(Str::AppTitle));
            showPage(kDownloads);
            refreshLists();
            SetTimer(hwnd_, kRefreshTimer, kRefreshMilliseconds, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == kRefreshTimer) onTimer();
            return 0;

        case WM_SIZE:
            layout();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {scale(kMinWidth), scale(kMinHeight)};
            return 0;
        }

        case WM_DPICHANGED: {
            applyDpi(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // tudo é pintado em WM_PAINT (sem piscar)

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            // Pinta numa imagem fora da tela e copia de uma vez.
            RECT client;
            GetClientRect(hwnd_, &client);
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ old = SelectObject(memory, bitmap);
            paint(memory);
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            SelectObject(memory, old);
            DeleteObject(bitmap);
            DeleteDC(memory);
            EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
                showPage(TabCtrl_GetCurSel(tabs_));
                return 0;
            }
            LRESULT result = 0;
            if (downloadsList_.handleNotify(header, result) || completedList_.handleNotify(header, result)) {
                return result;
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const HWND source = reinterpret_cast<HWND>(wParam);
            if (downloadsList_.handleContextMenu(source, point) || completedList_.handleContextMenu(source, point)) {
                return 0;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kIdAddButton:
                    if (HIWORD(wParam) == BN_CLICKED) onAddClicked();
                    return 0;
                case kTrayOpen:
                    showWindowFromTray();
                    return 0;
                case kTrayExit:
                    exitApp();
                    return 0;
            }
            break;

        case WM_COPYDATA: {
            // Responde logo (o dm-host.exe está esperando) e trata o pedido depois, fora desta chamada.
            const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (!data || !data->lpData) return FALSE;
            if (data->dwData == app::kCopyDataRequestStatus) {
                return browserRequestStatus(std::string(static_cast<const char*>(data->lpData), data->cbData));
            }
            if (data->dwData == app::kCopyDataAdopt) {
                auto adopt = dm::parseAdoptRequest(std::string(static_cast<const char*>(data->lpData), data->cbData));
                if (!adopt) return FALSE;
                pendingAdoptions_.push_back({std::move(*adopt), 0});
                PostMessageW(hwnd_, kProcessBrowserRequests, 0, 0);
                return TRUE;
            }
            if (data->dwData != app::kCopyDataBrowserRequest) return FALSE;
            const std::string json(static_cast<const char*>(data->lpData), data->cbData);
            auto request = dm::parseBrowserRequest(json);
            if (!request) return FALSE;
            if (!request->token.empty()) {
                if (browserTickets_.size() > 200) browserTickets_.clear();  // pedidos antigos e esquecidos
                browserTickets_[request->token] = BrowserTicket{};
            }
            pendingBrowserRequests_.push_back(std::move(*request));
            PostMessageW(hwnd_, kProcessBrowserRequests, 0, 0);
            return TRUE;
        }

        case kDebridChecked:
            onDebridChecked(lParam);
            return 0;

        case WM_DROPFILES:
            onDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;

        case app::kMessageQuit:
            quitByInstaller_ = true;
            exitApp();
            return 0;

        case kProcessBrowserRequests:
            processAdoptions();
            processBrowserRequests();
            while (!pendingWebPages_.empty() && !showingBrowserDialog_) {
                const dm::DownloadRecord record = pendingWebPages_.front();
                pendingWebPages_.erase(pendingWebPages_.begin());
                bool declined = false;
                showingBrowserDialog_ = true;
                addVideoFlow(dm::toWide(record.url), dm::toWide(record.directory), dm::toWide(record.fileName), {}, {},
                             declined);
                showingBrowserDialog_ = false;
            }
            return 0;

        case TrayIcon::kCallbackMessage:
            onTrayMessage(wParam, lParam);
            return 0;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor(reinterpret_cast<HDC>(wParam), kBackground);
            return reinterpret_cast<LRESULT>(background_);

        case WM_CLOSE:
            if (settings_.closeToTray && !exiting_) {
                ShowWindow(hwnd_, SW_HIDE);
                return 0;
            }
            exitApp();
            return 0;

        case WM_ENDSESSION:
            // Windows desligando: salva tudo para retomar na próxima vez.
            if (wParam) manager_->shutdown();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd_, kRefreshTimer);
            manager_->shutdown();
            tray_.remove();
            if (font_) DeleteObject(font_);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void MainWindow::loadSettings() {
    if (const auto text = dm::readTextFile(settingsPath_)) settings_ = dm::parseSettings(*text);
    // Mantém o registro em dia (por exemplo, se o .exe mudou de pasta).
    if (!app::testProfile()) app::setStartWithWindows(settings_.startWithWindows);
}

void MainWindow::saveSettings() {
    dm::writeTextFileAtomically(settingsPath_, dm::serializeSettings(settings_));
}

void MainWindow::loadRules() {
    const auto text = dm::readTextFile(rulesPath_);
    rules_ = text ? dm::parseRules(*text)
                  : dm::defaultRules(i18n::currentLanguage() == i18n::Language::Portuguese);
    applyRules();
}

void MainWindow::saveRules() {
    dm::writeTextFileAtomically(rulesPath_, dm::serializeRules(rules_));
}

void MainWindow::applyRules() {
    manager_->setRules(rules_, settings_.rulesEnabled, downloadFolder());
}

void MainWindow::applyQueueSettings() {
    manager_->setMaxRunning(settings_.maxDownloads);
    manager_->setSchedule(settings_.scheduleEnabled, settings_.scheduleStart, settings_.scheduleEnd);
    manager_->setGlobalSpeedLimit(settings_.speedLimitKBps * 1024);
}

void MainWindow::onSettingsChanged(const dm::Settings& settings) {
    const bool languageChanged = settings.language != settings_.language;
    const bool startupChanged = settings.startWithWindows != settings_.startWithWindows;
    if (settings.whenDone != settings_.whenDone) sawWork_ = !manager_->idle();
    settings_ = settings;
    saveSettings();
    applyQueueSettings();
    applyRules();  // a pasta padrão pode ter mudado
    if (startupChanged && !app::testProfile()) app::setStartWithWindows(settings_.startWithWindows);
    if (languageChanged) {
        applyLanguage();
        applyTexts();
    }
}

void MainWindow::applyLanguage() {
    switch (settings_.language) {
        case dm::LanguageSetting::Portuguese: i18n::setLanguage(i18n::Language::Portuguese); break;
        case dm::LanguageSetting::English: i18n::setLanguage(i18n::Language::English); break;
        default: i18n::setLanguage(i18n::systemLanguage()); break;
    }
}

void MainWindow::applyTexts() {
    SetWindowTextW(hwnd_, tr(Str::AppTitle));
    SetWindowTextW(addButton_, tr(Str::AddButton));
    tabCounts_.fill(static_cast<size_t>(-1));
    updateTabTitles();
    downloadsList_.applyTexts();
    completedList_.applyTexts();
    rulesPage_.applyTexts();
    settingsPage_.applyTexts();
    tray_.setTooltip(tr(Str::AppTitle));
    layout();
}

void MainWindow::updateTabTitles() {
    size_t active = 0;
    size_t completed = 0;
    for (const auto& item : manager_->items()) (item->completed() ? completed : active)++;
    const std::array<size_t, 2> counts{active, completed};
    if (counts == tabCounts_) return;
    tabCounts_ = counts;

    const Str titles[kPageCount] = {Str::TabDownloads, Str::TabCompleted, Str::TabRules, Str::TabSettings};
    for (int i = 0; i < kPageCount; ++i) {
        std::wstring text = tr(titles[i]);
        if (i < 2 && counts[i] > 0) text += L" (" + std::to_wstring(counts[i]) + L")";
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        TabCtrl_SetItem(tabs_, i, &item);
    }
    InvalidateRect(tabs_, nullptr, TRUE);
}

void MainWindow::createControls() {
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, 0, 0, 0,
                            0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTabs)), instance_, nullptr);
    for (int i = 0; i < kPageCount; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"");
        TabCtrl_InsertItem(tabs_, i, &item);
    }

    addButton_ = CreateWindowExW(0, WC_BUTTONW, tr(Str::AddButton), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAddButton)),
                                 instance_, nullptr);

    pages_[kDownloads] = downloadsList_.create(hwnd_, DownloadListView::Mode::Active, *manager_);
    pages_[kCompleted] = completedList_.create(hwnd_, DownloadListView::Mode::Completed, *manager_);
    downloadsList_.onChanged = [this] { refreshLists(); };
    completedList_.onChanged = [this] { refreshLists(); };

    rulesPage_.onChanged = [this](const std::vector<dm::Rule>& rules, bool enabled) {
        rules_ = rules;
        saveRules();
        if (enabled != settings_.rulesEnabled) {
            settings_.rulesEnabled = enabled;
            saveSettings();
        }
        applyRules();
    };
    pages_[kRules] = rulesPage_.create(hwnd_);
    rulesPage_.setRules(rules_, settings_.rulesEnabled);

    settingsPage_.onChanged = [this](const dm::Settings& settings) { onSettingsChanged(settings); };
    settingsPage_.onUpdateButton = [this] {
        if (updater_->ready()) {
            if (installUpdate(L"/abrir")) exitApp();
        } else {
            updater_->checkIfDue(true);
            refreshUpdateStatus();
        }
    };
    settingsPage_.onDebridConnect = [this](const std::string& token) { checkDebridAccount(token, true); };
    settingsPage_.onDebridDisconnect = [this] {
        settings_.realDebridToken.clear();
        saveSettings();
        applyDebridToken();
        settingsPage_.setDebridStatus(tr(Str::SettingsDebridNotConnected), false);
    };
    pages_[kSettings] = settingsPage_.create(hwnd_);
    settingsPage_.setSettings(settings_);
    updateTabTitles();
}

void MainWindow::applyDpi(UINT dpi) {
    dpi_ = dpi;

    LOGFONTW logFont{};
    logFont.lfHeight = -MulDiv(9, static_cast<int>(dpi_), 72);
    logFont.lfWeight = FW_NORMAL;
    logFont.lfCharSet = DEFAULT_CHARSET;
    logFont.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(logFont.lfFaceName, L"Segoe UI", LF_FACESIZE);
    HFONT newFont = CreateFontIndirectW(&logFont);

    for (HWND control : {tabs_, addButton_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
    }
    // As listas usam a fonte do tema (Segoe UI Variable no Windows 11).
    if (listFont_) DeleteObject(listFont_);
    listFont_ = theme::createFont(9, FW_NORMAL, dpi_);
    downloadsList_.applyDpi(dpi_);
    completedList_.applyDpi(dpi_);
    downloadsList_.setFont(listFont_);
    completedList_.setFont(listFont_);
    rulesPage_.applyDpi(dpi_);

    if (font_) DeleteObject(font_);
    font_ = newFont;
    layout();
}

void MainWindow::layout() {
    if (!tabs_) return;

    RECT client;
    GetClientRect(hwnd_, &client);
    const int margin = scale(kMargin);
    const int buttonWidth = scale(kAddButtonWidth);
    footerHeight_ = scale(kFooterHeight);

    // A barra de abas ocupa só a altura dos cabeçalhos; as páginas ficam abaixo, sobre o fundo branco.
    RECT firstTab{};
    TabCtrl_GetItemRect(tabs_, 0, &firstTab);
    const int tabHeight = firstTab.bottom + scale(2);

    MoveWindow(tabs_, margin, margin, client.right - buttonWidth - margin * 3, tabHeight, TRUE);
    MoveWindow(addButton_, client.right - margin - buttonWidth, margin, buttonWidth, tabHeight - scale(2), TRUE);

    const int pageTop = margin + tabHeight + scale(4);
    const int pageHeight = std::max<int>(0, client.bottom - pageTop - footerHeight_);
    for (int i = 0; i < kPageCount; ++i) {
        const int inset = i == kSettings || i == kRules ? scale(8) : 0;
        MoveWindow(pages_[i], margin + inset, pageTop + inset, client.right - margin * 2 - inset, pageHeight - inset,
                   TRUE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::paint(HDC dc) {
    RECT client;
    GetClientRect(hwnd_, &client);
    theme::fillRect(dc, client, kBackground);
    SetBkMode(dc, TRANSPARENT);

    // Rodapé: o que está acontecendo, discreto.
    RECT footerLine{0, client.bottom - footerHeight_, client.right, client.bottom - footerHeight_ + 1};
    theme::fillRect(dc, footerLine, theme::kHairline);
    if (listFont_) SelectObject(dc, listFont_);
    RECT footer{scale(kMargin) + scale(4), client.bottom - footerHeight_, client.right - scale(kMargin) - scale(4),
                client.bottom};
    SetTextColor(dc, theme::kTextSecondary);
    DrawTextW(dc, footerText_.c_str(), static_cast<int>(footerText_.size()), &footer,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (!footerSpeed_.empty()) {
        SetTextColor(dc, theme::kText);
        DrawTextW(dc, footerSpeed_.c_str(), static_cast<int>(footerSpeed_.size()), &footer,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void MainWindow::updateFooter() {
    int running = 0;
    int queued = 0;
    double speed = 0;
    for (const auto& item : manager_->items()) {
        if (item->running()) {
            ++running;
            if (item->live.remote == dm::RemoteStage::None) speed += item->speed();
        } else if (item->queued()) {
            ++queued;
        }
    }
    wchar_t text[128];
    std::wstring status;
    if (running > 0) {
        std::swprintf(text, 128, tr(Str::FooterDownloading), running);
        status = text;
    }
    if (queued > 0) {
        std::swprintf(text, 128, tr(Str::FooterQueued), queued);
        status += (status.empty() ? L"" : L"  ·  ") + std::wstring(text);
    }
    if (status.empty()) status = tr(Str::FooterIdle);
    const std::wstring speedText =
        speed > 0 ? L"↓ " + dm::toWide(dm::formatSpeed(speed, i18n::decimalSeparator())) : L"";
    if (status == footerText_ && speedText == footerSpeed_) return;
    footerText_ = status;
    footerSpeed_ = speedText;
    RECT client;
    GetClientRect(hwnd_, &client);
    RECT footer{0, client.bottom - footerHeight_, client.right, client.bottom};
    InvalidateRect(hwnd_, &footer, FALSE);
}

void MainWindow::showPage(int page) {
    if (page < 0 || page >= kPageCount) return;
    currentPage_ = page;
    TabCtrl_SetCurSel(tabs_, page);
    for (int i = 0; i < kPageCount; ++i) ShowWindow(pages_[i], i == page ? SW_SHOW : SW_HIDE);
}

void MainWindow::refreshLists() {
    downloadsList_.refresh();
    completedList_.refresh();
    updateTabTitles();
}

void MainWindow::onTimer() {
    if (!pendingAdoptions_.empty()) processAdoptions();
    const bool changed = manager_->tick();
    updateTick();
    if (exiting_) return;

    const bool working = manager_->runningCount() > 0;
    if (settings_.keepAwake && working != keepingAwake_) {
        keepingAwake_ = working;
        app::keepSystemAwake(working);
    } else if (!settings_.keepAwake && keepingAwake_) {
        keepingAwake_ = false;
        app::keepSystemAwake(false);
    }

    if (!IsWindowVisible(hwnd_)) {  // na bandeja: não gasta tempo pintando
        checkWhenDone();
        return;
    }
    if (changed) {
        refreshLists();
    } else if (currentPage_ == kDownloads) {
        downloadsList_.refresh();
    }
    updateFooter();
    checkWhenDone();
}

bool MainWindow::installUpdate(const wchar_t* relaunch) {
    if (updateLaunched_) return true;
    // Cópia solta (não instalada pelo setup): abre a janela do instalador em vez de instalar calado.
    updateLaunched_ = updater_->launchInstaller(relaunch, installedCopy_);
    return updateLaunched_;
}

void MainWindow::refreshUpdateStatus() {
    const app::Updater::State state = updater_->state();
    lastUpdateState_ = static_cast<int>(state);
    wchar_t text[512];
    std::swprintf(text, 512, tr(Str::UpdateVersion), dm::toWide(DM_VERSION_STRING).c_str());
    std::wstring status = text;
    const std::wstring version = updater_->availableVersion();
    switch (state) {
        case app::Updater::State::Checking: status += L" · " + std::wstring(tr(Str::UpdateChecking)); break;
        case app::Updater::State::Current: status += L" · " + std::wstring(tr(Str::UpdateCurrent)); break;
        case app::Updater::State::Failed: status += L" · " + std::wstring(tr(Str::UpdateFailed)); break;
        case app::Updater::State::Downloading:
            std::swprintf(text, 512, tr(Str::UpdateDownloading), version.c_str());
            status += L" · " + std::wstring(text);
            break;
        case app::Updater::State::Ready:
            std::swprintf(text, 512, tr(Str::UpdateReady), version.c_str());
            status += L" · " + std::wstring(text);
            break;
        default: break;
    }
    settingsPage_.setUpdateStatus(status, state == app::Updater::State::Ready);
}

void MainWindow::updateTick() {
    // Primeira procura 1 minuto depois de abrir; depois o Updater só procura uma vez por dia.
    if (settings_.autoUpdate && ++updateTicks_ % (60000 / kRefreshMilliseconds) == 0) updater_->checkIfDue();
    if (static_cast<int>(updater_->state()) != lastUpdateState_) refreshUpdateStatus();

    // Instala sozinho só quando ninguém nota: janela na bandeja, nada baixando, nenhum diálogo aberto.
    if (settings_.autoUpdate && installedCopy_ && updater_->ready() && !updater_->alreadyAttempted() &&
        !IsWindowVisible(hwnd_) && manager_->idle() && !showingBrowserDialog_ && GetLastActivePopup(hwnd_) == hwnd_ && installUpdate(L"/bandeja")) {
        exitApp();
    }
}

void MainWindow::checkWhenDone() {
    if (settings_.whenDone == dm::WhenDone::Nothing) return;
    if (!manager_->idle()) {
        sawWork_ = true;
    } else if (sawWork_) {
        sawWork_ = false;
        runWhenDoneAction();
    }
}

void MainWindow::markOrganize(uint64_t id, const std::wstring& folder) {
    // Só organiza o que foi para a pasta padrão; quem escolheu outra pasta já decidiu onde fica.
    if (id != 0) manager_->setOrganize(id, sameFolder(folder, downloadFolder()));
}

std::wstring MainWindow::downloadFolder() const {
    return settings_.downloadFolder.empty() ? app::defaultDownloadFolder() : dm::toWide(settings_.downloadFolder);
}

void MainWindow::prepareAddRequest(AddRequest& request) const {
    request.folder = downloadFolder();
    request.defaultFolder = request.folder;
    request.rules = &rules_;
    request.rulesEnabled = settings_.rulesEnabled;
    request.organize = true;  // sem diálogo, vai para a pasta padrão e as regras organizam
}

void MainWindow::onAddClicked() {
    AddRequest request;
    prepareAddRequest(request);
    if (!showAddDialog(hwnd_, request)) return;
    if (request.folder.empty()) request.folder = downloadFolder();

    if (request.torrent) {
        addTorrent(request.url, request.folder, request.organize);
    } else if (dm::looksLikeVideoPage(dm::toUtf8(request.url))) {
        SHCreateDirectoryExW(nullptr, request.folder.c_str(), nullptr);
        bool declined = false;
        addVideoFlow(request.url, request.folder, request.fileName, {}, {}, declined);
    } else {
        SHCreateDirectoryExW(nullptr, request.folder.c_str(), nullptr);
        const uint64_t id = manager_->add(dm::toUtf8(request.url), request.folder, request.fileName,
                                          settings_.connections, {}, {}, /*rejectWebPages=*/true);
        manager_->setOrganize(id, request.organize);
    }
    showPage(kDownloads);
    refreshLists();
}

uint64_t MainWindow::addTorrent(std::wstring input, const std::wstring& folder, bool organize) {
    if (input.size() > 2 && input.front() == L'"' && input.back() == L'"') input = input.substr(1, input.size() - 2);
    if (settings_.realDebridToken.empty()) {
        MessageBoxW(IsWindowVisible(hwnd_) ? hwnd_ : nullptr, tr(Str::TorrentNeedsDebrid), tr(Str::AppTitle),
                    MB_OK | MB_ICONINFORMATION);
        showWindowFromTray();
        showPage(kSettings);
        return 0;
    }
    std::string source = dm::toUtf8(input);
    std::wstring name;
    if (dm::isMagnetLink(source)) {
        name = dm::toWide(dm::magnetDisplayName(source));
    } else {
        // Guarda uma cópia: o original pode ser apagado ou movido antes de o Real-Debrid receber.
        const std::wstring folderCopy = dm::joinPath(app::dataDirectory(), L"torrents");
        SHCreateDirectoryExW(nullptr, folderCopy.c_str(), nullptr);
        name = dm::fileNameOf(input);
        const std::wstring copy = dm::uniquePath(folderCopy, name);
        if (!CopyFileW(input.c_str(), copy.c_str(), TRUE)) return 0;
        source = dm::toUtf8(copy);
        if (name.size() > 8 && _wcsicmp(name.c_str() + name.size() - 8, L".torrent") == 0) name.resize(name.size() - 8);
    }
    SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
    const uint64_t id = manager_->addTorrent(source, folder, name, settings_.connections);
    manager_->setOrganize(id, organize);
    refreshLists();
    return id;
}

void MainWindow::applyDebridToken() {
    std::string token;
    if (!settings_.realDebridToken.empty()) {
        if (const auto plain = dm::unprotectForCurrentUser(settings_.realDebridToken)) token = *plain;
    }
    manager_->setDebridToken(std::move(token));
}

void MainWindow::checkDebridAccount(const std::string& token, bool connecting) {
    settingsPage_.setDebridStatus(tr(Str::SettingsDebridChecking), !connecting, true);
    const HWND window = hwnd_;
    std::thread([window, token, connecting] {
        auto* result = new DebridCheckResult{app::checkDebridAccount(token), token, connecting};
        if (!PostMessageW(window, kDebridChecked, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }).detach();
}

void MainWindow::onDebridChecked(LPARAM lParam) {
    std::unique_ptr<DebridCheckResult> result(reinterpret_cast<DebridCheckResult*>(lParam));
    const app::DebridAccountCheck& check = result->check;
    if (check.ok) {
        if (result->connecting) {
            settings_.realDebridToken = dm::protectForCurrentUser(result->token);
            saveSettings();
            applyDebridToken();
        }
        wchar_t text[300];
        const std::wstring user = dm::toWide(check.user.username);
        const std::wstring until = dm::toWide(
            dm::formatDebridDate(check.user.expiration, i18n::currentLanguage() == i18n::Language::Portuguese));
        if (check.user.premium && !until.empty()) {
            std::swprintf(text, 300, tr(Str::SettingsDebridConnected), user.c_str(), until.c_str());
        } else {
            std::swprintf(text, 300, tr(Str::SettingsDebridConnectedFree), user.c_str());
        }
        settingsPage_.setDebridStatus(text, true);
        return;
    }
    const std::wstring reason = check.networkError != 0
                                    ? i18n::describeError(dm::DownloadError::Network, check.networkError)
                                    : i18n::describeDebridError(check.error);
    wchar_t text[400];
    std::swprintf(text, 400, tr(Str::SettingsDebridFailed), reason.c_str());
    // Conferindo um token já salvo: continua conectado (pode ser só a internet); o usuário decide desconectar.
    settingsPage_.setDebridStatus(text, !result->connecting && !settings_.realDebridToken.empty());
}

void MainWindow::onDropFiles(HDROP drop) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    bool added = false;
    for (UINT i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH];
        if (!DragQueryFileW(drop, i, path, MAX_PATH) || !isTorrentInput(path)) continue;
        if (addTorrent(path, downloadFolder(), true) == 0 && settings_.realDebridToken.empty()) break;
        added = true;
    }
    DragFinish(drop);
    if (added) showPage(kDownloads);
}

uint64_t MainWindow::addVideoFlow(const std::wstring& url, const std::wstring& folder, const std::wstring& title,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  const std::string& userAgent, bool& declined) {
    VideoRequest request{url, folder, title, headers, userAgent};
    VideoChoice choice;
    declined = !showVideoDialog(IsWindowVisible(hwnd_) ? hwnd_ : nullptr, *videoTools_, request, choice);
    if (declined) return 0;
    if (choice.folder.empty()) choice.folder = folder;
    SHCreateDirectoryExW(nullptr, choice.folder.c_str(), nullptr);

    if (choice.downloadAsFile) {
        const uint64_t id = manager_->add(dm::toUtf8(url), choice.folder, {}, settings_.connections, headers, userAgent);
        markOrganize(id, choice.folder);
        return id;
    }
    uint64_t first = 0;
    for (const auto& item : choice.items) {
        const uint64_t id = manager_->addVideo(dm::toUtf8(item.url), choice.folder, item.title, choice.format,
                                               choice.subtitles, headers, userAgent);
        markOrganize(id, choice.folder);
        if (first == 0) first = id;
    }
    refreshLists();
    return first;
}

void MainWindow::processBrowserRequests() {
    // Um diálogo de cada vez; os outros pedidos esperam a vez.
    if (showingBrowserDialog_) return;
    while (!pendingBrowserRequests_.empty()) {
        const dm::BrowserRequest request = pendingBrowserRequests_.front();
        pendingBrowserRequests_.erase(pendingBrowserRequests_.begin());
        onBrowserRequest(request);
    }
}

void MainWindow::onBrowserRequest(const dm::BrowserRequest& browserRequest) {
    if (dm::isMagnetLink(browserRequest.url)) {
        // Magnet pelo clique direito: vai para o Real-Debrid (a extensão não espera resposta).
        browserTickets_.erase(browserRequest.token);
        if (addTorrent(dm::toWide(browserRequest.url), downloadFolder(), true) != 0) showPage(kDownloads);
        return;
    }
    AddRequest request;
    prepareAddRequest(request);
    request.fromBrowser = true;
    request.url = dm::toWide(browserRequest.url);
    request.fileName = dm::toWide(dm::sanitizeFileName(browserRequest.fileName));
    if (browserRequest.fileName.empty()) request.fileName.clear();
    request.headers = dm::browserHeaders(browserRequest);
    request.userAgent = browserRequest.userAgent;

    auto ticket = browserTickets_.find(browserRequest.token);
    const bool isVideo = browserRequest.source == dm::BrowserRequest::Source::Page ||
                         (browserRequest.source != dm::BrowserRequest::Source::Capture &&
                          dm::looksLikeVideoPage(browserRequest.url));
    if (isVideo) {
        // Vídeo sempre abre o diálogo: é preciso escolher a qualidade.
        showingBrowserDialog_ = true;
        bool declined = false;
        const uint64_t id = addVideoFlow(request.url, request.folder, request.fileName, request.headers,
                                         request.userAgent, declined);
        showingBrowserDialog_ = false;
        ticket = browserTickets_.find(browserRequest.token);
        if (ticket != browserTickets_.end()) ticket->second = {false, declined || id == 0, id};
        return;
    }
    if (settings_.askForBrowserDownloads) {
        showingBrowserDialog_ = true;
        const bool confirmed = showAddDialog(IsWindowVisible(hwnd_) ? hwnd_ : nullptr, request);
        showingBrowserDialog_ = false;
        ticket = browserTickets_.find(browserRequest.token);  // o mapa pode ter mudado durante o diálogo
        if (!confirmed) {
            if (ticket != browserTickets_.end()) ticket->second = {false, true, 0};
            return;
        }
    }
    if (request.folder.empty()) request.folder = downloadFolder();
    SHCreateDirectoryExW(nullptr, request.folder.c_str(), nullptr);
    const uint64_t id = manager_->add(dm::toUtf8(request.url), request.folder, request.fileName, settings_.connections,
                                      request.headers, request.userAgent);
    manager_->setOrganize(id, request.organize);
    if (ticket != browserTickets_.end()) ticket->second = {false, false, id};
    refreshLists();
}

LRESULT MainWindow::browserRequestStatus(const std::string& token) {
    const auto ticket = browserTickets_.find(token);
    if (ticket == browserTickets_.end()) return app::kStatusUnknown;
    if (ticket->second.pending) return app::kStatusWaiting;
    if (ticket->second.declined) {
        browserTickets_.erase(ticket);
        return app::kStatusDeclined;
    }

    const uint64_t id = ticket->second.id;
    const app::DownloadItem* item = manager_->find(id);
    if (!item) {
        browserTickets_.erase(ticket);
        return app::kStatusUnknown;
    }
    if (item->record.state == dm::RecordState::Failed) {
        // O navegador continua sozinho e o app adota o arquivo no fim; este item sairia duplicado.
        browserTickets_.erase(ticket);
        manager_->remove(id, true);
        refreshLists();
        return app::kStatusFailed;
    }
    if (item->running() && item->live.status == dm::DownloadStatus::Connecting) return app::kStatusWaiting;
    browserTickets_.erase(ticket);
    return app::kStatusStarted;
}

void MainWindow::processAdoptions() {
    for (size_t i = 0; i < pendingAdoptions_.size();) {
        PendingAdoption& adoption = pendingAdoptions_[i];
        // O navegador (ou o antivírus) pode segurar o arquivo por alguns segundos depois de terminar.
        if (adoptFile(adoption.request) || ++adoption.attempts >= 20) {
            pendingAdoptions_.erase(pendingAdoptions_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

bool MainWindow::adoptFile(const dm::AdoptRequest& request) {
    if (!settings_.adoptBrowserDownloads) return true;
    const std::wstring source = dm::toWide(request.path);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &info)) {
        return GetLastError() != ERROR_SHARING_VIOLATION;  // sumiu: nada a fazer
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return true;
    const int64_t size = (static_cast<int64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;

    // Só lista: mover o arquivo depois de pronto quebra o "Mostrar na pasta" do navegador. No Chrome/Edge
    // ele já foi salvo na subpasta da regra (a extensão pergunta antes); no Firefox fica onde o navegador pôs.
    manager_->addCompleted(request.url, source, size);
    refreshLists();
    return true;
}

void MainWindow::onTrayMessage(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONDBLCLK:
            showWindowFromTray();
            break;
        case NIN_BALLOONUSERCLICK:
            if (const app::DownloadItem* item = manager_->find(lastCompletedId_)) {
                app::showInFolder(dm::toWide(item->record.filePath));
            }
            break;
        case WM_CONTEXTMENU: {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kTrayOpen, tr(Str::TrayOpen));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kTrayExit, tr(Str::TrayExit));
            SetMenuDefaultItem(menu, kTrayOpen, FALSE);
            SetForegroundWindow(hwnd_);  // sem isso o menu não fecha ao clicar fora
            TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), 0,
                           hwnd_, nullptr);
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            DestroyMenu(menu);
            break;
        }
    }
}

void MainWindow::showWindowFromTray() {
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
    refreshLists();
}

namespace {

struct Countdown {
    Str message;
    int secondsLeft;
};

HRESULT CALLBACK countdownCallback(HWND dialog, UINT notification, WPARAM wParam, LPARAM, LONG_PTR data) {
    auto* countdown = reinterpret_cast<Countdown*>(data);
    auto updateText = [&] {
        wchar_t text[256];
        swprintf(text, 256, tr(countdown->message), countdown->secondsLeft);
        SendMessageW(dialog, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(text));
    };
    if (notification == TDN_CREATED) {
        SendMessageW(dialog, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, countdown->secondsLeft));
        SendMessageW(dialog, TDM_SET_PROGRESS_BAR_POS, countdown->secondsLeft, 0);
        updateText();
    } else if (notification == TDN_TIMER) {
        // wParam: milissegundos desde a criação (ou desde o último reset).
        const int elapsed = static_cast<int>(wParam / 1000);
        if (elapsed >= 1) {
            countdown->secondsLeft -= elapsed;
            if (countdown->secondsLeft <= 0) {
                SendMessageW(dialog, TDM_CLICK_BUTTON, IDOK, 0);
                return S_OK;
            }
            SendMessageW(dialog, TDM_SET_PROGRESS_BAR_POS, countdown->secondsLeft, 0);
            updateText();
            return S_FALSE;  // reinicia o relógio do TDN_TIMER
        }
    }
    return S_OK;
}

}  // namespace

void MainWindow::runWhenDoneAction() {
    const dm::WhenDone action = settings_.whenDone;
    // Vale uma vez só: volta para "Não fazer nada" antes de agir.
    settings_.whenDone = dm::WhenDone::Nothing;
    settingsPage_.setSettings(settings_);

    Countdown countdown{action == dm::WhenDone::Shutdown ? Str::CountdownShutdown : Str::CountdownSleep, 60};
    const TASKDIALOG_BUTTON buttons[] = {{IDOK, tr(Str::CountdownNow)}, {IDCANCEL, tr(Str::Cancel)}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.hwndParent = IsWindowVisible(hwnd_) ? hwnd_ : nullptr;
    config.dwFlags = TDF_CALLBACK_TIMER | TDF_SHOW_PROGRESS_BAR | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pButtons = buttons;
    config.cButtons = 2;
    config.nDefaultButton = IDCANCEL;
    config.pszWindowTitle = tr(Str::AppTitle);
    config.pszMainInstruction = tr(Str::CountdownTitle);
    config.pszContent = L"";
    config.hMainIcon = iconLarge_;
    config.dwFlags |= TDF_USE_HICON_MAIN;
    config.pfCallback = countdownCallback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&countdown);

    int pressed = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)) || pressed != IDOK) return;

    if (action == dm::WhenDone::Shutdown) {
        manager_->shutdown();
        app::shutdownComputer();
    } else {
        manager_->save();
        app::sleepComputer();
    }
}

void MainWindow::exitApp() {
    // Fechando o app: se houver versão nova baixada, o instalador troca os arquivos sem reabrir.
    if (!quitByInstaller_ && settings_.autoUpdate && installedCopy_ && updater_->ready() &&
        !updater_->alreadyAttempted()) {
        installUpdate(nullptr);
    }
    exiting_ = true;
    DestroyWindow(hwnd_);
}

}  // namespace ui
