#include "ui/dialogs.h"

#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>

#include <cstdio>
#include <cwctype>

#include "app/system.h"
#include "core/debrid.h"
#include "core/http_headers.h"
#include "i18n/strings.h"
#include "resource.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

std::wstring trimmed(std::wstring text) {
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    size_t start = 0;
    while (start < text.size() && std::iswspace(text[start])) ++start;
    return text.substr(start);
}

constexpr COLORREF kHintColor = RGB(107, 114, 128);

// Estado do diálogo "Adicionar" enquanto está aberto.
struct AddDialogState {
    AddRequest* request = nullptr;
    std::wstring autoFolder;      // o que o diálogo colocou em "Salvar em" (padrão ou pasta da regra)
    bool folderEdited = false;    // o usuário escolheu outra pasta: a escolha dele vale
    bool settingFolder = false;
    bool linkShown = false;
    HFONT nameFont = nullptr;
    HICON icon = nullptr;
};

AddDialogState* stateOf(HWND dialog) {
    return reinterpret_cast<AddDialogState*>(GetWindowLongPtrW(dialog, DWLP_USER));
}

std::wstring lowerExtension(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || name.find_first_of(L"\\/", dot) != std::wstring::npos) return {};
    std::wstring extension = name.substr(dot + 1);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    return extension;
}

// Nome que o arquivo vai ter: o digitado, ou o do link.
std::wstring effectiveName(HWND dialog) {
    std::wstring name = trimmed(windowText(GetDlgItem(dialog, IDC_NAME)));
    if (name.empty()) name = dm::toWide(dm::fileNameFromUrl(dm::toUtf8(trimmed(windowText(GetDlgItem(dialog, IDC_URL))))));
    return name;
}

// Põe em "Salvar em" a pasta onde o arquivo vai parar de verdade (a da regra que casar), enquanto o
// usuário não escolher outra.
void updatePredictedFolder(HWND dialog) {
    AddDialogState* state = stateOf(dialog);
    if (!state) return;
    const AddRequest& request = *state->request;
    const std::wstring url = trimmed(windowText(GetDlgItem(dialog, IDC_URL)));
    const bool torrent = isTorrentInput(url);

    std::wstring folder = request.defaultFolder;
    std::wstring hint;
    if (torrent) {
        hint = tr(Str::AddTorrentHint);
    } else if (request.rules && request.rulesEnabled && !url.empty()) {
        const dm::DownloadFacts facts{dm::toUtf8(url), dm::toUtf8(effectiveName(dialog)), -1, false};
        if (const dm::Rule* rule = dm::matchRule(*request.rules, facts)) {
            folder = dm::toWide(dm::resolveRuleFolder(rule->folder, dm::toUtf8(request.defaultFolder)));
            wchar_t text[256];
            std::swprintf(text, 256, tr(Str::AddRuleHint), dm::toWide(rule->name).c_str());
            hint = text;
        }
    }
    if (!state->folderEdited) {
        state->settingFolder = true;
        SetDlgItemTextW(dialog, IDC_FOLDER, folder.c_str());
        state->settingFolder = false;
        state->autoFolder = folder;
        SetDlgItemTextW(dialog, IDC_RULE_HINT, hint.c_str());
    } else {
        SetDlgItemTextW(dialog, IDC_RULE_HINT, torrent ? hint.c_str() : L"");
    }
}

// Mostra/esconde o link no diálogo do navegador, empurrando o resto para baixo.
void toggleLink(HWND dialog) {
    AddDialogState* state = stateOf(dialog);
    state->linkShown = !state->linkShown;
    RECT offset{0, 0, 0, 18};
    MapDialogRect(dialog, &offset);
    const int dy = state->linkShown ? offset.bottom : -offset.bottom;
    for (int id : {IDC_FOLDER_LABEL, IDC_FOLDER, IDC_BROWSE, IDC_RULE_HINT, IDOK, IDCANCEL}) {
        HWND control = GetDlgItem(dialog, id);
        RECT rect;
        GetWindowRect(control, &rect);
        MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rect), 2);
        SetWindowPos(control, nullptr, rect.left, rect.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT window;
    GetWindowRect(dialog, &window);
    SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left, window.bottom - window.top + dy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(GetDlgItem(dialog, IDC_URL), state->linkShown ? SW_SHOW : SW_HIDE);
    const std::wstring link = std::wstring(L"<a>") + tr(state->linkShown ? Str::AddHideLink : Str::AddShowLink) + L"</a>";
    SetDlgItemTextW(dialog, IDC_SHOW_LINK, link.c_str());
    InvalidateRect(dialog, nullptr, TRUE);
}

std::wstring chooseTorrentFile(HWND owner) {
    wchar_t path[MAX_PATH] = L"";
    std::wstring filter = tr(Str::TorrentFilter);
    filter += L'\0';
    filter += L"*.torrent";
    filter += L'\0';
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? path : L"";
}

void initAddDialog(HWND dialog, AddDialogState* state) {
    AddRequest* request = state->request;
    SetWindowTextW(dialog, tr(Str::AddTitle));
    SetDlgItemTextW(dialog, IDC_URL_LABEL, tr(Str::AddUrlLabel));
    SetDlgItemTextW(dialog, IDC_FOLDER_LABEL, tr(Str::AddFolderLabel));
    SetDlgItemTextW(dialog, IDC_NAME_LABEL, tr(Str::AddNameLabel));
    SetDlgItemTextW(dialog, IDC_BROWSE, tr(Str::Browse));
    SetDlgItemTextW(dialog, IDC_TORRENT, tr(Str::AddTorrentButton));
    SetDlgItemTextW(dialog, IDOK, tr(Str::AddStart));
    SetDlgItemTextW(dialog, IDCANCEL, tr(Str::Cancel));
    SetDlgItemTextW(dialog, IDC_URL, request->url.c_str());
    if (request->defaultFolder.empty()) request->defaultFolder = request->folder;

    if (request->fromBrowser) {
        // Sem nome do navegador, o servidor decide; o do link aparece só como dica.
        std::wstring name = request->fileName;
        SetDlgItemTextW(dialog, IDC_NAME, name.c_str());
        if (name.empty()) {
            name = dm::toWide(dm::fileNameFromUrl(dm::toUtf8(request->url)));
            SendDlgItemMessageW(dialog, IDC_NAME, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(name.c_str()));
        }

        // De onde veio: o site da página (Referer), não o servidor de arquivos.
        std::wstring site;
        for (const auto& [header, value] : request->headers) {
            if (_stricmp(header.c_str(), "Referer") == 0) site = siteOf(dm::toWide(value));
        }
        if (site.empty()) site = siteOf(request->url);
        wchar_t source[300];
        std::swprintf(source, 300, tr(Str::AddFromSite), site.c_str());
        SetDlgItemTextW(dialog, IDC_SOURCE, source);
        const std::wstring link = std::wstring(L"<a>") + tr(Str::AddShowLink) + L"</a>";
        SetDlgItemTextW(dialog, IDC_SHOW_LINK, link.c_str());

        SHFILEINFOW info{};
        const std::wstring extension = lowerExtension(name);
        const std::wstring probe = extension.empty() ? L"arquivo" : L"arquivo." + extension;
        if (SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES)) {
            state->icon = info.hIcon;
            SendDlgItemMessageW(dialog, IDC_FILE_ICON, STM_SETICON, reinterpret_cast<WPARAM>(info.hIcon), 0);
        }

        // Nome do arquivo em destaque: um pouco maior e semibold.
        LOGFONTW font{};
        font.lfHeight = -MulDiv(11, static_cast<int>(GetDpiForWindow(dialog)), 72);
        font.lfWeight = FW_SEMIBOLD;
        font.lfCharSet = DEFAULT_CHARSET;
        font.lfQuality = CLEARTYPE_QUALITY;
        lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
        state->nameFont = CreateFontIndirectW(&font);
        SendDlgItemMessageW(dialog, IDC_NAME, WM_SETFONT, reinterpret_cast<WPARAM>(state->nameFont), TRUE);
    } else {
        SetDlgItemTextW(dialog, IDC_NAME, request->fileName.c_str());
    }
    updatePredictedFolder(dialog);
    SetForegroundWindow(dialog);  // pode ter vindo do navegador, com o app na bandeja
    // Pedido do navegador já tem link: o foco vai para o botão Baixar (Enter confirma).
    SetFocus(GetDlgItem(dialog, request->url.empty() ? IDC_URL : IDOK));
}

INT_PTR CALLBACK addDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CTLCOLORSTATIC) {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id == IDC_RULE_HINT || id == IDC_SOURCE) {
            const INT_PTR brush = whiteBackground(message, wParam);
            SetTextColor(reinterpret_cast<HDC>(wParam), kHintColor);
            return brush;
        }
    }
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;

    switch (message) {
        case WM_INITDIALOG: {
            auto* state = new AddDialogState;
            state->request = reinterpret_cast<AddRequest*>(lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            initAddDialog(dialog, state);
            return FALSE;  // foco já definido
        }
        case WM_DESTROY:
            if (AddDialogState* state = stateOf(dialog)) {
                if (state->nameFont) DeleteObject(state->nameFont);
                if (state->icon) DestroyIcon(state->icon);
                delete state;
                SetWindowLongPtrW(dialog, DWLP_USER, 0);
            }
            return FALSE;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->idFrom == IDC_SHOW_LINK && (header->code == NM_CLICK || header->code == NM_RETURN)) {
                toggleLink(dialog);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_URL:
                case IDC_NAME:
                    if (HIWORD(wParam) == EN_CHANGE) updatePredictedFolder(dialog);
                    return TRUE;
                case IDC_FOLDER:
                    if (HIWORD(wParam) == EN_CHANGE) {
                        AddDialogState* state = stateOf(dialog);
                        if (state && !state->settingFolder) {
                            state->folderEdited = trimmed(windowText(GetDlgItem(dialog, IDC_FOLDER))) != state->autoFolder;
                            if (state->folderEdited) updatePredictedFolder(dialog);
                        }
                    }
                    return TRUE;
                case IDC_BROWSE: {
                    const std::wstring folder = app::chooseFolder(dialog, tr(Str::ChooseFolder),
                                                                  windowText(GetDlgItem(dialog, IDC_FOLDER)));
                    if (!folder.empty()) SetDlgItemTextW(dialog, IDC_FOLDER, folder.c_str());
                    return TRUE;
                }
                case IDC_TORRENT: {
                    const std::wstring path = chooseTorrentFile(dialog);
                    if (!path.empty()) {
                        SetDlgItemTextW(dialog, IDC_URL, path.c_str());
                        SetFocus(GetDlgItem(dialog, IDOK));
                    }
                    return TRUE;
                }
                case IDOK: {
                    AddDialogState* state = stateOf(dialog);
                    AddRequest* request = state->request;
                    const std::wstring url = trimmed(windowText(GetDlgItem(dialog, IDC_URL)));
                    const bool torrent = isTorrentInput(url);
                    if (!isWebUrl(url) && !torrent) {
                        MessageBoxW(dialog, tr(Str::InvalidUrlMessage), tr(Str::AddTitle), MB_OK | MB_ICONWARNING);
                        SetFocus(GetDlgItem(dialog, IDC_URL));
                        return TRUE;
                    }
                    request->url = url;
                    request->torrent = torrent;
                    request->folder = trimmed(windowText(GetDlgItem(dialog, IDC_FOLDER)));
                    request->fileName = trimmed(windowText(GetDlgItem(dialog, IDC_NAME)));
                    request->organize = !state->folderEdited || request->folder == state->autoFolder;
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dialog, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK changeUrlDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;

    switch (message) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            SetWindowTextW(dialog, tr(Str::ChangeUrlTitle));
            SetDlgItemTextW(dialog, IDC_URL_LABEL, tr(Str::ChangeUrlLabel));
            SetDlgItemTextW(dialog, IDC_URL, reinterpret_cast<std::wstring*>(lParam)->c_str());
            SetDlgItemTextW(dialog, IDOK, tr(Str::Save));
            SetDlgItemTextW(dialog, IDCANCEL, tr(Str::Cancel));
            SendDlgItemMessageW(dialog, IDC_URL, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, IDC_URL));
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                const std::wstring url = trimmed(windowText(GetDlgItem(dialog, IDC_URL)));
                if (!isWebUrl(url)) {
                    MessageBoxW(dialog, tr(Str::InvalidUrlMessage), tr(Str::ChangeUrlTitle), MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                *reinterpret_cast<std::wstring*>(GetWindowLongPtrW(dialog, DWLP_USER)) = url;
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

INT_PTR CALLBACK speedLimitDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;

    switch (message) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            SetWindowTextW(dialog, tr(Str::SpeedLimitTitle));
            SetDlgItemTextW(dialog, IDC_URL_LABEL, tr(Str::SpeedLimitLabel));
            SetDlgItemInt(dialog, IDC_URL, static_cast<UINT>(*reinterpret_cast<int64_t*>(lParam)), FALSE);
            SetDlgItemTextW(dialog, IDOK, tr(Str::Save));
            SetDlgItemTextW(dialog, IDCANCEL, tr(Str::Cancel));
            SendDlgItemMessageW(dialog, IDC_URL, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, IDC_URL));
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                BOOL valid = FALSE;
                const UINT value = GetDlgItemInt(dialog, IDC_URL, &valid, FALSE);
                *reinterpret_cast<int64_t*>(GetWindowLongPtrW(dialog, DWLP_USER)) = valid ? value : 0;
                EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace

INT_PTR whiteBackground(UINT message, WPARAM wParam) {
    static HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
        return reinterpret_cast<INT_PTR>(white);
    }
    return 0;
}

std::wstring windowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

bool isWebUrl(const std::wstring& url) {
    auto startsWith = [&](const wchar_t* prefix) { return _wcsnicmp(url.c_str(), prefix, wcslen(prefix)) == 0; };
    return (startsWith(L"http://") && url.size() > 7) || (startsWith(L"https://") && url.size() > 8);
}

bool isTorrentInput(const std::wstring& text) {
    if (dm::isMagnetLink(dm::toUtf8(text))) return true;
    std::wstring path = text;
    if (path.size() > 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    return lowerExtension(path) == L"torrent" && dm::fileExists(path);
}

std::wstring siteOf(const std::wstring& url) {
    const size_t scheme = url.find(L"://");
    if (scheme == std::wstring::npos) return {};
    const size_t start = scheme + 3;
    std::wstring host = url.substr(start, url.find_first_of(L"/?#", start) - start);
    if (const size_t at = host.rfind(L'@'); at != std::wstring::npos) host = host.substr(at + 1);
    if (const size_t colon = host.find(L':'); colon != std::wstring::npos) host.resize(colon);
    for (wchar_t& c : host) c = static_cast<wchar_t>(std::towlower(c));
    if (host.rfind(L"www.", 0) == 0) host = host.substr(4);
    return host;
}

bool showAddDialog(HWND owner, AddRequest& request) {
    const int id = request.fromBrowser ? IDD_ADD_BROWSER : IDD_ADD;
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), owner, addDialogProc,
                           reinterpret_cast<LPARAM>(&request)) == IDOK;
}

bool showChangeUrlDialog(HWND owner, std::wstring& url) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_CHANGE_URL), owner, changeUrlDialogProc,
                           reinterpret_cast<LPARAM>(&url)) == IDOK;
}

bool showSpeedLimitDialog(HWND owner, int64_t& kilobytesPerSecond) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SPEED_LIMIT), owner, speedLimitDialogProc,
                           reinterpret_cast<LPARAM>(&kilobytesPerSecond)) == IDOK;
}

}  // namespace ui
