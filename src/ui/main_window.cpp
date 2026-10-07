#include "ui/main_window.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>

#include "app/system.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
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

void MainWindow::onSettingsChanged(const dm::Settings& settings) {
    const bool languageChanged = settings.language != settings_.language;
    const bool startupChanged = settings.startWithWindows != settings_.startWithWindows;
    settings_ = settings;
    saveSettings();
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
    const bool changed = manager_->tick();
    if (!IsWindowVisible(hwnd_)) return;  // na bandeja: não gasta tempo pintando
    if (changed) {
        refreshLists();
    } else if (currentPage_ == kDownloads) {
        downloadsList_.refresh();
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

void MainWindow::exitApp() {
    exiting_ = true;
    DestroyWindow(hwnd_);
}

}  // namespace ui
