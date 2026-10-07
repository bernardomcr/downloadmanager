#include "ui/dialogs.h"

#include <cwctype>

#include "app/system.h"
#include "i18n/strings.h"
#include "resource.h"

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

INT_PTR CALLBACK addDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;

    switch (message) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            const auto* request = reinterpret_cast<AddRequest*>(lParam);
            SetWindowTextW(dialog, tr(Str::AddTitle));
            SetDlgItemTextW(dialog, IDC_URL_LABEL, tr(Str::AddUrlLabel));
            SetDlgItemTextW(dialog, IDC_FOLDER_LABEL, tr(Str::AddFolderLabel));
            SetDlgItemTextW(dialog, IDC_NAME_LABEL, tr(Str::AddNameLabel));
            SetDlgItemTextW(dialog, IDC_BROWSE, tr(Str::Browse));
            SetDlgItemTextW(dialog, IDOK, tr(Str::AddStart));
            SetDlgItemTextW(dialog, IDCANCEL, tr(Str::Cancel));
            SetDlgItemTextW(dialog, IDC_URL, request->url.c_str());
            SetDlgItemTextW(dialog, IDC_FOLDER, request->folder.c_str());
            SetDlgItemTextW(dialog, IDC_NAME, request->fileName.c_str());
            SetForegroundWindow(dialog);  // pode ter vindo do navegador, com o app na bandeja
            // Pedido do navegador já tem link: o foco vai para o botão Baixar (Enter confirma).
            SetFocus(GetDlgItem(dialog, request->url.empty() ? IDC_URL : IDOK));
            return FALSE;  // foco já definido
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BROWSE: {
                    const std::wstring folder = app::chooseFolder(dialog, tr(Str::ChooseFolder),
                                                                  windowText(GetDlgItem(dialog, IDC_FOLDER)));
                    if (!folder.empty()) SetDlgItemTextW(dialog, IDC_FOLDER, folder.c_str());
                    return TRUE;
                }
                case IDOK: {
                    auto* request = reinterpret_cast<AddRequest*>(GetWindowLongPtrW(dialog, DWLP_USER));
                    const std::wstring url = trimmed(windowText(GetDlgItem(dialog, IDC_URL)));
                    if (!isWebUrl(url)) {
                        MessageBoxW(dialog, tr(Str::InvalidUrlMessage), tr(Str::AddTitle), MB_OK | MB_ICONWARNING);
                        SetFocus(GetDlgItem(dialog, IDC_URL));
                        return TRUE;
                    }
                    request->url = url;
                    request->folder = trimmed(windowText(GetDlgItem(dialog, IDC_FOLDER)));
                    request->fileName = trimmed(windowText(GetDlgItem(dialog, IDC_NAME)));
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

bool showAddDialog(HWND owner, AddRequest& request) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADD), owner, addDialogProc,
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
