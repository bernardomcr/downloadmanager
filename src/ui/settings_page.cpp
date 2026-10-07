#include "ui/settings_page.h"

#include <commctrl.h>

#include "app/system.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {

HWND SettingsPage::create(HWND parent) {
    dialog_ = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), parent, dialogProc,
                                 reinterpret_cast<LPARAM>(this));
    return dialog_;
}

void SettingsPage::setSettings(const dm::Settings& settings) {
    settings_ = settings;
    fillControls();
}

void SettingsPage::applyTexts() {
    SetDlgItemTextW(dialog_, IDC_SET_FOLDER_LABEL, tr(Str::SettingsFolder));
    SetDlgItemTextW(dialog_, IDC_SET_BROWSE, tr(Str::Browse));
    SetDlgItemTextW(dialog_, IDC_SET_CONNECTIONS_LABEL, tr(Str::SettingsConnections));
    SetDlgItemTextW(dialog_, IDC_SET_LANGUAGE_LABEL, tr(Str::SettingsLanguage));
    SetDlgItemTextW(dialog_, IDC_SET_CLOSE_TO_TRAY, tr(Str::SettingsCloseToTray));
    SetDlgItemTextW(dialog_, IDC_SET_START_WITH_WINDOWS, tr(Str::SettingsStartWithWindows));
    SetDlgItemTextW(dialog_, IDC_SET_NOTIFY, tr(Str::SettingsNotify));
    fillControls();
}

void SettingsPage::fillControls() {
    if (!dialog_) return;
    filling_ = true;
    const std::wstring folder =
        settings_.downloadFolder.empty() ? app::defaultDownloadFolder() : dm::toWide(settings_.downloadFolder);
    SetDlgItemTextW(dialog_, IDC_SET_FOLDER, folder.c_str());

    SendDlgItemMessageW(dialog_, IDC_SET_CONNECTIONS_SPIN, UDM_SETRANGE32, 1, 32);
    SendDlgItemMessageW(dialog_, IDC_SET_CONNECTIONS_SPIN, UDM_SETPOS32, 0, settings_.connections);

    HWND language = GetDlgItem(dialog_, IDC_SET_LANGUAGE);
    SendMessageW(language, CB_RESETCONTENT, 0, 0);
    for (Str option : {Str::LanguageAutomatic, Str::LanguagePortuguese, Str::LanguageEnglish}) {
        SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(option)));
    }
    SendMessageW(language, CB_SETCURSEL, static_cast<WPARAM>(settings_.language), 0);

    CheckDlgButton(dialog_, IDC_SET_CLOSE_TO_TRAY, settings_.closeToTray ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_START_WITH_WINDOWS, settings_.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_NOTIFY, settings_.notifyOnComplete ? BST_CHECKED : BST_UNCHECKED);
    filling_ = false;
}

void SettingsPage::notify() {
    if (!filling_ && onChanged) onChanged(settings_);
}

INT_PTR CALLBACK SettingsPage::dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* self = reinterpret_cast<SettingsPage*>(lParam);
        self->dialog_ = dialog;
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        self->applyTexts();
        return TRUE;
    }
    auto* self = reinterpret_cast<SettingsPage*>(GetWindowLongPtrW(dialog, DWLP_USER));
    return self ? self->handleMessage(message, wParam, lParam) : FALSE;
}

INT_PTR SettingsPage::handleMessage(UINT message, WPARAM wParam, LPARAM /*lParam*/) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;
    if (message != WM_COMMAND || filling_) return FALSE;

    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);
    switch (id) {
        case IDC_SET_BROWSE: {
            const std::wstring folder = app::chooseFolder(dialog_, tr(Str::ChooseFolder),
                                                          windowText(GetDlgItem(dialog_, IDC_SET_FOLDER)));
            if (folder.empty()) return TRUE;
            SetDlgItemTextW(dialog_, IDC_SET_FOLDER, folder.c_str());
            settings_.downloadFolder = dm::toUtf8(folder);
            notify();
            return TRUE;
        }
        case IDC_SET_FOLDER:
            if (code == EN_KILLFOCUS) {
                const std::string folder = dm::toUtf8(windowText(GetDlgItem(dialog_, IDC_SET_FOLDER)));
                if (folder != settings_.downloadFolder) {
                    settings_.downloadFolder = folder;
                    notify();
                }
            }
            return TRUE;
        case IDC_SET_CONNECTIONS:
            if (code == EN_CHANGE) {
                BOOL valid = FALSE;
                const UINT value = GetDlgItemInt(dialog_, IDC_SET_CONNECTIONS, &valid, FALSE);
                if (valid && value >= 1 && value <= 32 && static_cast<int>(value) != settings_.connections) {
                    settings_.connections = static_cast<int>(value);
                    notify();
                }
            }
            return TRUE;
        case IDC_SET_LANGUAGE:
            if (code == CBN_SELCHANGE) {
                const auto selected = SendDlgItemMessageW(dialog_, IDC_SET_LANGUAGE, CB_GETCURSEL, 0, 0);
                if (selected >= 0) {
                    settings_.language = static_cast<dm::LanguageSetting>(selected);
                    notify();
                }
            }
            return TRUE;
        case IDC_SET_CLOSE_TO_TRAY:
            settings_.closeToTray = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_START_WITH_WINDOWS:
            settings_.startWithWindows = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_NOTIFY:
            settings_.notifyOnComplete = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
    }
    return FALSE;
}

}  // namespace ui
