#include "ui/main_window.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "app/browser_integration.h"
#include "app/system.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "core/http_headers.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

// Medidas em pixels lógicos (96 DPI); convertidas com scale().
constexpr int kWindowWidth = 790;
constexpr int kWindowHeight = 460;
constexpr int kMinWidth = 560;
constexpr int kMinHeight = 320;
constexpr int kMargin = 8;
constexpr int kAddButtonWidth = 104;
constexpr COLORREF kBackground = RGB(255, 255, 255);

constexpr int kIdTabs = 100;
constexpr int kIdAddButton = 101;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kProcessBrowserRequests = WM_APP + 2;
constexpr UINT kRefreshMilliseconds = 500;

enum TrayCommand { kTrayOpen = 3001, kTrayExit };

struct Column {
    Str title;
    int width;
};
constexpr Column kRuleColumns[] = {{Str::ColCondition, 340}, {Str::ColAction, 340}};

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
    app::registerBrowserIntegration(dataDirectory);
    loadSettings();
    applyLanguage();
    manager_ = std::make_unique<app::DownloadManager>(dm::joinPath(dataDirectory, L"downloads.dat"));

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

    hwnd_ = CreateWindowExW(0, kClassName, tr(Str::AppTitle), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            kWindowWidth, kWindowHeight, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

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
    HWND page = settingsPage_.handle();
    return page && currentPage_ == kSettings && IsChild(page, message.hwnd) && IsDialogMessageW(page, &message);
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
                lastCompletedPath_ = dm::toWide(item.record.filePath);
                if (settings_.notifyOnComplete) {
                    tray_.showNotification(tr(Str::NotifyCompleted), dm::fileNameOf(lastCompletedPath_));
                }
            };
            applyQueueSettings();
            manager_->load();
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

        case kProcessBrowserRequests:
            processAdoptions();
            processBrowserRequests();
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
    app::setStartWithWindows(settings_.startWithWindows);
}

void MainWindow::saveSettings() {
    dm::writeTextFileAtomically(settingsPath_, dm::serializeSettings(settings_));
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
    if (startupChanged) app::setStartWithWindows(settings_.startWithWindows);
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
    for (int i = 0; i < static_cast<int>(std::size(kRuleColumns)); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(tr(kRuleColumns[i].title));
        ListView_SetColumn(rulesList_, i, &column);
    }
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

    rulesList_ = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_NOSORTHEADER, 0, 0,
                                 0, 0, hwnd_, nullptr, instance_, nullptr);
    SetWindowTheme(rulesList_, L"Explorer", nullptr);
    for (int i = 0; i < static_cast<int>(std::size(kRuleColumns)); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = kRuleColumns[i].width;
        column.pszText = const_cast<wchar_t*>(tr(kRuleColumns[i].title));
        ListView_InsertColumn(rulesList_, i, &column);
    }
    pages_[kRules] = rulesList_;

    settingsPage_.onChanged = [this](const dm::Settings& settings) { onSettingsChanged(settings); };
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

    for (HWND control : {tabs_, addButton_, pages_[kDownloads], pages_[kCompleted], rulesList_}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
    }
    downloadsList_.applyDpi(dpi_);
    completedList_.applyDpi(dpi_);
    for (int i = 0; i < static_cast<int>(std::size(kRuleColumns)); ++i) {
        ListView_SetColumnWidth(rulesList_, i, scale(kRuleColumns[i].width));
    }

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

    // A barra de abas ocupa só a altura dos cabeçalhos; as páginas ficam abaixo, sobre o fundo branco.
    RECT firstTab{};
    TabCtrl_GetItemRect(tabs_, 0, &firstTab);
    const int tabHeight = firstTab.bottom + scale(2);

    MoveWindow(tabs_, margin, margin, client.right - buttonWidth - margin * 3, tabHeight, TRUE);
    MoveWindow(addButton_, client.right - margin - buttonWidth, margin, buttonWidth, tabHeight - scale(2), TRUE);

    const int pageTop = margin + tabHeight + scale(4);
    const int pageHeight = client.bottom - pageTop - margin;
    for (int i = 0; i < kPageCount; ++i) {
        const int inset = i == kSettings ? scale(4) : 0;
        MoveWindow(pages_[i], margin + inset, pageTop + inset, client.right - margin * 2 - inset, pageHeight - inset,
                   TRUE);
    }
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
    checkWhenDone();
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

std::wstring MainWindow::downloadFolder() const {
    return settings_.downloadFolder.empty() ? app::defaultDownloadFolder() : dm::toWide(settings_.downloadFolder);
}

void MainWindow::onAddClicked() {
    AddRequest request;
    request.folder = downloadFolder();
    if (!showAddDialog(hwnd_, request)) return;
    if (request.folder.empty()) request.folder = downloadFolder();
    SHCreateDirectoryExW(nullptr, request.folder.c_str(), nullptr);

    manager_->add(dm::toUtf8(request.url), request.folder, request.fileName, settings_.connections);
    showPage(kDownloads);
    refreshLists();
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
    AddRequest request;
    request.url = dm::toWide(browserRequest.url);
    request.folder = downloadFolder();
    request.fileName = dm::toWide(dm::sanitizeFileName(browserRequest.fileName));
    if (browserRequest.fileName.empty()) request.fileName.clear();
    request.headers = dm::browserHeaders(browserRequest);
    request.userAgent = browserRequest.userAgent;

    auto ticket = browserTickets_.find(browserRequest.token);
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

    const std::wstring folder = downloadFolder();
    std::wstring target = source;
    if (_wcsicmp(dm::directoryOf(source).c_str(), folder.c_str()) != 0) {
        SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        target = dm::uniquePath(folder, dm::fileNameOf(source));
        if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) return false;
    }
    manager_->addCompleted(request.url, target, size);
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
            if (!lastCompletedPath_.empty()) app::showInFolder(lastCompletedPath_);
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
    exiting_ = true;
    DestroyWindow(hwnd_);
}

}  // namespace ui
