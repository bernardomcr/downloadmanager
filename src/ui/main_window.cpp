#include "ui/main_window.h"

#include <commctrl.h>

#include <initializer_list>
#include <span>

#include "i18n/strings.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

// Medidas em pixels lógicos (96 DPI); convertidas com scale().
constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 460;
constexpr int kMinWidth = 560;
constexpr int kMinHeight = 320;
constexpr int kMargin = 8;
constexpr int kAddButtonWidth = 104;
constexpr COLORREF kBackground = RGB(255, 255, 255);

constexpr int kIdTabs = 100;
constexpr int kIdAddButton = 101;

struct Column {
    Str title;
    int width;
    int format = LVCFMT_LEFT;
};

constexpr Column kDownloadColumns[] = {
    {Str::ColName, 230},
    {Str::ColSize, 80, LVCFMT_RIGHT},
    {Str::ColProgress, 90, LVCFMT_RIGHT},
    {Str::ColSpeed, 90, LVCFMT_RIGHT},
    {Str::ColTimeLeft, 110, LVCFMT_RIGHT},
    {Str::ColStatus, 110},
};

constexpr Column kCompletedColumns[] = {
    {Str::ColName, 280},
    {Str::ColSize, 80, LVCFMT_RIGHT},
    {Str::ColFolder, 200},
    {Str::ColFinishedAt, 130},
};

constexpr Column kRuleColumns[] = {
    {Str::ColCondition, 340},
    {Str::ColAction, 340},
};

std::span<const Column> columnsFor(int page) {
    switch (page) {
        case 0: return kDownloadColumns;
        case 1: return kCompletedColumns;
        case 2: return kRuleColumns;
        default: return {};
    }
}

HWND createList(HWND parent, HINSTANCE instance, std::span<const Column> columns) {
    HWND list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                                0, 0, 0, 0, parent, nullptr, instance, nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = columns[i].format;
        column.cx = columns[i].width;
        column.pszText = const_cast<wchar_t*>(tr(columns[i].title));
        ListView_InsertColumn(list, i, &column);
    }
    return list;
}

}  // namespace

bool MainWindow::create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
    background_ = CreateSolidBrush(kBackground);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background_;
    windowClass.lpszClassName = kClassName;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&windowClass)) return false;

    hwnd_ = CreateWindowExW(0, kClassName, tr(Str::AppTitle), WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
                            nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    // A janela foi criada no DPI do monitor; ajusta o tamanho inicial para ele.
    SetWindowPos(hwnd_, nullptr, 0, 0, scale(kWindowWidth), scale(kWindowHeight),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
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
    return self ? self->handleMessage(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            dpi_ = GetDpiForWindow(hwnd_);
            createControls();
            applyDpi(dpi_);
            showPage(kDownloads);
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
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }

        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
                showPage(TabCtrl_GetCurSel(tabs_));
            }
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == kIdAddButton && HIWORD(wParam) == BN_CLICKED) {
                onAddClicked();
            }
            return 0;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor(reinterpret_cast<HDC>(wParam), kBackground);
            return reinterpret_cast<LRESULT>(background_);

        case WM_DESTROY:
            if (font_) DeleteObject(font_);
            if (background_) DeleteObject(background_);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void MainWindow::createControls() {
    tabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(kIdTabs), instance_, nullptr);
    const Str tabTitles[kPageCount] = {Str::TabDownloads, Str::TabCompleted, Str::TabRules,
                                       Str::TabSettings};
    for (int i = 0; i < kPageCount; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(tr(tabTitles[i]));
        TabCtrl_InsertItem(tabs_, i, &item);
    }

    addButton_ = CreateWindowExW(0, WC_BUTTONW, tr(Str::AddButton),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                                 reinterpret_cast<HMENU>(kIdAddButton), instance_, nullptr);

    pages_[kDownloads] = createList(hwnd_, instance_, kDownloadColumns);
    pages_[kCompleted] = createList(hwnd_, instance_, kCompletedColumns);
    pages_[kRules] = createList(hwnd_, instance_, kRuleColumns);
    pages_[kSettings] = CreateWindowExW(0, WC_STATICW, tr(Str::ComingSoon), WS_CHILD | SS_CENTER,
                                        0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
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
    for (int page = 0; page < kPageCount; ++page) {
        SendMessageW(pages_[page], WM_SETFONT, reinterpret_cast<WPARAM>(newFont), TRUE);
        const auto columns = columnsFor(page);
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            ListView_SetColumnWidth(pages_[page], i, scale(columns[i].width));
        }
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

    const int tabsWidth = client.right - buttonWidth - margin * 3;
    MoveWindow(tabs_, margin, margin, tabsWidth, tabHeight, TRUE);
    MoveWindow(addButton_, client.right - margin - buttonWidth, margin, buttonWidth,
               tabHeight - scale(2), TRUE);

    const int pageTop = margin + tabHeight + scale(4);
    for (HWND page : pages_) {
        MoveWindow(page, margin, pageTop, client.right - margin * 2, client.bottom - pageTop - margin,
                   TRUE);
    }
}

void MainWindow::showPage(int page) {
    if (page < 0 || page >= kPageCount) return;
    currentPage_ = page;
    for (int i = 0; i < kPageCount; ++i) {
        ShowWindow(pages_[i], i == page ? SW_SHOW : SW_HIDE);
    }
}

void MainWindow::onAddClicked() {
    // Fase 2: diálogo "Adicionar" que detecta link HTTP, vídeo, magnet ou curso.
    MessageBoxW(hwnd_, tr(Str::ComingSoon), tr(Str::AppTitle), MB_OK | MB_ICONINFORMATION);
}

}  // namespace ui
