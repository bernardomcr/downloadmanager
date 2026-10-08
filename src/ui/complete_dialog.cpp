#include "ui/complete_dialog.h"

#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <thread>
#include <vector>

#include "app/extractor.h"
#include "app/system.h"
#include "core/archive.h"
#include "core/format.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "ui/theme.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

constexpr UINT kExtractDone = WM_APP + 1;  // wParam: 1 = sucesso; lParam: std::wstring* com o caminho
constexpr UINT_PTR kRefreshTimer = 1;
constexpr COLORREF kHintColor = RGB(107, 114, 128);

struct CompleteState {
    uint64_t id = 0;
    PathLookup lookup;
    bool extractNow = false;
    bool extracting = false;
    std::wstring extractedPath;  // depois de extrair, Abrir/Abrir pasta vão para o que saiu do compactado
    std::wstring shownPath;
    HFONT nameFont = nullptr;
    HICON icon = nullptr;
};

std::vector<HWND> g_windows;

CompleteState* stateOf(HWND dialog) {
    return reinterpret_cast<CompleteState*>(GetWindowLongPtrW(dialog, DWLP_USER));
}

std::wstring currentPath(const CompleteState& state) {
    return state.lookup ? state.lookup(state.id) : std::wstring();
}

std::wstring lowerExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring extension = name.substr(dot + 1);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    return extension;
}

// Nome, tamanho e pasta; o ícone segue o tipo do arquivo.
void refreshInfo(HWND dialog) {
    CompleteState* state = stateOf(dialog);
    const std::wstring path = currentPath(*state);
    if (path.empty() || path == state->shownPath) return;
    state->shownPath = path;
    const std::wstring name = dm::fileNameOf(path);
    SetDlgItemTextW(dialog, IDC_COMPLETE_NAME, name.c_str());

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    std::wstring info = dm::directoryOf(path);
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) &&
        !(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        const int64_t size = (static_cast<int64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        info = dm::toWide(dm::formatBytes(size, i18n::decimalSeparator())) + L"  ·  " + info;
    }
    SetDlgItemTextW(dialog, IDC_COMPLETE_INFO, info.c_str());

    SHFILEINFOW file{};
    if (SHGetFileInfoW(path.c_str(), 0, &file, sizeof(file), SHGFI_ICON | SHGFI_LARGEICON)) {
        if (state->icon) DestroyIcon(state->icon);
        state->icon = file.hIcon;
        SendDlgItemMessageW(dialog, IDC_FILE_ICON, STM_SETICON, reinterpret_cast<WPARAM>(file.hIcon), 0);
    }
    const bool archive = dm::isArchiveExtension(dm::toUtf8(lowerExtension(name)));
    ShowWindow(GetDlgItem(dialog, IDC_COMPLETE_EXTRACT), archive ? SW_SHOW : SW_HIDE);
}

void startExtract(HWND dialog) {
    CompleteState* state = stateOf(dialog);
    const std::wstring path = currentPath(*state);
    if (state->extracting || path.empty()) return;
    const app::Extractor extractor = app::findExtractorFor(path);
    if (extractor.kind == app::Extractor::Kind::None) {
        SetDlgItemTextW(dialog, IDC_COMPLETE_STATUS, tr(Str::CompleteNoExtractor));
        return;
    }
    state->extracting = true;
    EnableWindow(GetDlgItem(dialog, IDC_COMPLETE_EXTRACT), FALSE);
    wchar_t text[256];
    std::swprintf(text, 256, tr(Str::CompleteExtracting), extractor.name.c_str());
    SetDlgItemTextW(dialog, IDC_COMPLETE_STATUS, text);
    // O extrator mostra a janela de progresso dele; aqui só espera o fim, fora da thread da interface.
    std::thread([dialog, path] {
        const app::ExtractResult result = app::smartExtract(path);
        auto* extracted = new std::wstring(result.path);
        if (!PostMessageW(dialog, kExtractDone, result.ok ? 1 : 0, reinterpret_cast<LPARAM>(extracted))) {
            delete extracted;
        }
    }).detach();
}

// Canto inferior direito da área de trabalho, empilhando as janelas abertas.
void placeWindow(HWND dialog) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT rect;
    GetWindowRect(dialog, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int gap = MulDiv(12, static_cast<int>(GetDpiForWindow(dialog)), 96);
    const int stacked = static_cast<int>(g_windows.size()) - 1;
    int top = work.bottom - gap - height - stacked * (height + gap / 2);
    if (top < work.top) top = work.bottom - gap - height;
    SetWindowPos(dialog, HWND_TOPMOST, work.right - gap - width, top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

INT_PTR CALLBACK completeProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CTLCOLORSTATIC) {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id == IDC_COMPLETE_INFO || id == IDC_COMPLETE_STATUS) {
            const INT_PTR brush = whiteBackground(message, wParam);
            SetTextColor(reinterpret_cast<HDC>(wParam), kHintColor);
            return brush;
        }
    }
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;

    switch (message) {
        case WM_INITDIALOG: {
            auto* state = reinterpret_cast<CompleteState*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            SetWindowTextW(dialog, tr(Str::CompleteTitle));
            SetDlgItemTextW(dialog, IDC_COMPLETE_OPEN, tr(Str::CompleteOpen));
            SetDlgItemTextW(dialog, IDC_COMPLETE_FOLDER, tr(Str::CompleteOpenFolder));
            SetDlgItemTextW(dialog, IDC_COMPLETE_EXTRACT, tr(Str::CompleteExtract));
            SetDlgItemTextW(dialog, IDCANCEL, tr(Str::CompleteClose));
            state->nameFont = theme::createFont(10, FW_SEMIBOLD, GetDpiForWindow(dialog));
            SendDlgItemMessageW(dialog, IDC_COMPLETE_NAME, WM_SETFONT, reinterpret_cast<WPARAM>(state->nameFont), TRUE);
            theme::applyWindowFrame(dialog);
            if (HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP),
                                                           IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                           GetSystemMetrics(SM_CYSMICON), 0))) {
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
            }
            refreshInfo(dialog);
            SetTimer(dialog, kRefreshTimer, 1000, nullptr);
            if (state->extractNow) startExtract(dialog);
            return FALSE;  // não pega o foco
        }
        case WM_TIMER:
            if (wParam == kRefreshTimer && !stateOf(dialog)->extracting) refreshInfo(dialog);
            return TRUE;
        case kExtractDone: {
            CompleteState* state = stateOf(dialog);
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
            state->extracting = false;
            if (wParam) {
                state->extractedPath = *path;
                wchar_t text[MAX_PATH + 64];
                std::swprintf(text, std::size(text), tr(Str::CompleteExtracted), path->c_str());
                SetDlgItemTextW(dialog, IDC_COMPLETE_STATUS, text);
            } else {
                SetDlgItemTextW(dialog, IDC_COMPLETE_STATUS, tr(Str::CompleteExtractFailed));
                EnableWindow(GetDlgItem(dialog, IDC_COMPLETE_EXTRACT), TRUE);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            CompleteState* state = stateOf(dialog);
            const std::wstring target = state->extractedPath.empty() ? currentPath(*state) : state->extractedPath;
            switch (LOWORD(wParam)) {
                case IDC_COMPLETE_OPEN:
                    if (!target.empty()) app::openFile(target);
                    DestroyWindow(dialog);
                    return TRUE;
                case IDC_COMPLETE_FOLDER:
                    if (!target.empty()) app::showInFolder(target);
                    DestroyWindow(dialog);
                    return TRUE;
                case IDC_COMPLETE_EXTRACT:
                    startExtract(dialog);
                    return TRUE;
                case IDCANCEL:
                    DestroyWindow(dialog);
                    return TRUE;
            }
            break;
        }
        case WM_DESTROY: {
            KillTimer(dialog, kRefreshTimer);
            std::erase(g_windows, dialog);
            if (CompleteState* state = stateOf(dialog)) {
                if (state->nameFont) DeleteObject(state->nameFont);
                if (state->icon) DestroyIcon(state->icon);
                delete state;
                SetWindowLongPtrW(dialog, DWLP_USER, 0);
            }
            return TRUE;
        }
    }
    return FALSE;
}

}  // namespace

void showCompleteWindow(uint64_t id, PathLookup lookup, bool extractNow) {
    // Mesmo download já aberto (ex.: "Extrair" pelo menu): reaproveita a janela.
    for (HWND window : g_windows) {
        if (CompleteState* state = stateOf(window); state && state->id == id) {
            if (extractNow) startExtract(window);
            SetForegroundWindow(window);
            return;
        }
    }
    auto* state = new CompleteState{id, std::move(lookup), extractNow};
    HWND dialog = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_COMPLETE), nullptr, completeProc,
                                     reinterpret_cast<LPARAM>(state));
    if (!dialog) {
        delete state;
        return;
    }
    g_windows.push_back(dialog);
    placeWindow(dialog);
    ShowWindow(dialog, extractNow ? SW_SHOW : SW_SHOWNOACTIVATE);
}

bool completeWindowMessage(MSG& message) {
    const std::vector<HWND> windows = g_windows;  // Esc fecha a janela e muda a lista no meio
    for (HWND window : windows) {
        if ((message.hwnd == window || IsChild(window, message.hwnd)) && IsDialogMessageW(window, &message)) return true;
    }
    return false;
}

}  // namespace ui
