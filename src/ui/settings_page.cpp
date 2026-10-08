#include "ui/settings_page.h"

#include <commctrl.h>

#include <algorithm>
#include <cctype>

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
    SetDlgItemTextW(dialog_, IDC_SET_KEEP_AWAKE, tr(Str::SettingsKeepAwake));
    SetDlgItemTextW(dialog_, IDC_SET_BROWSER_ASK, tr(Str::SettingsBrowserAsk));
    SetDlgItemTextW(dialog_, IDC_SET_ADOPT, tr(Str::SettingsAdopt));
    SetDlgItemTextW(dialog_, IDC_SET_BROWSER_FOLDER, tr(Str::SettingsBrowserFolder));
    SetDlgItemTextW(dialog_, IDC_SET_COMPLETE_WINDOW, tr(Str::SettingsCompleteWindow));
    SetDlgItemTextW(dialog_, IDC_SET_SPEED_UNIT_LABEL, tr(Str::SettingsSpeedUnit));
    SetDlgItemTextW(dialog_, IDC_SET_AUTO_UPDATE, tr(Str::SettingsAutoUpdate));
    SetDlgItemTextW(dialog_, IDC_SET_UPDATE, tr(updateReady_ ? Str::UpdateNow : Str::UpdateCheckNow));
    SetDlgItemTextW(dialog_, IDC_SET_MAX_DOWNLOADS_LABEL, tr(Str::SettingsMaxDownloads));
    SetDlgItemTextW(dialog_, IDC_SET_SPEED_LIMIT_LABEL, tr(Str::SettingsSpeedLimit));
    SetDlgItemTextW(dialog_, IDC_SET_SCHEDULE, tr(Str::SettingsSchedule));
    SetDlgItemTextW(dialog_, IDC_SET_SCHEDULE_AND, tr(Str::SettingsScheduleAnd));
    SetDlgItemTextW(dialog_, IDC_SET_WHEN_DONE_LABEL, tr(Str::SettingsWhenDone));
    for (int id : {IDC_SET_SCHEDULE_START, IDC_SET_SCHEDULE_END}) {
        SendDlgItemMessageW(dialog_, id, DTM_SETFORMATW, 0, reinterpret_cast<LPARAM>(L"HH':'mm"));
    }
    SetDlgItemTextW(dialog_, IDC_SET_DEBRID_TITLE, tr(Str::SettingsDebridTitle));
    SendDlgItemMessageW(dialog_, IDC_SET_DEBRID_TOKEN, EM_SETCUEBANNER, TRUE,
                        reinterpret_cast<LPARAM>(tr(Str::SettingsDebridToken)));
    if (!titleFont_) {
        LOGFONTW font{};
        font.lfHeight = -MulDiv(9, static_cast<int>(GetDpiForWindow(dialog_)), 72);
        font.lfWeight = FW_SEMIBOLD;
        font.lfCharSet = DEFAULT_CHARSET;
        font.lfQuality = CLEARTYPE_QUALITY;
        lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
        titleFont_ = CreateFontIndirectW(&font);
        SendDlgItemMessageW(dialog_, IDC_SET_DEBRID_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    }
    if (debridStatus_.empty()) debridStatus_ = tr(Str::SettingsDebridNotConnected);
    setDebridStatus(debridStatus_, debridConnected_, debridChecking_);
    fillControls();
}

void SettingsPage::setDebridStatus(const std::wstring& text, bool connected, bool checking) {
    debridStatus_ = text;
    debridConnected_ = connected;
    debridChecking_ = checking;
    if (!dialog_) return;
    SetDlgItemTextW(dialog_, IDC_SET_DEBRID_STATUS, text.c_str());
    SetDlgItemTextW(dialog_, IDC_SET_DEBRID_CONNECT,
                    tr(connected ? Str::SettingsDebridDisconnect : Str::SettingsDebridConnect));
    HWND token = GetDlgItem(dialog_, IDC_SET_DEBRID_TOKEN);
    // Conectado: o token não fica à mostra nem editável (está guardado criptografado).
    if (connected) SetWindowTextW(token, L"");
    EnableWindow(token, !connected && !checking);
    EnableWindow(GetDlgItem(dialog_, IDC_SET_DEBRID_CONNECT), !checking);
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
    CheckDlgButton(dialog_, IDC_SET_KEEP_AWAKE, settings_.keepAwake ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_BROWSER_ASK, settings_.askForBrowserDownloads ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_ADOPT, settings_.adoptBrowserDownloads ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_BROWSER_FOLDER, settings_.browserFolder ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_COMPLETE_WINDOW, settings_.showCompleteWindow ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dialog_, IDC_SET_AUTO_UPDATE, settings_.autoUpdate ? BST_CHECKED : BST_UNCHECKED);
    HWND speedUnit = GetDlgItem(dialog_, IDC_SET_SPEED_UNIT);
    SendMessageW(speedUnit, CB_RESETCONTENT, 0, 0);
    for (Str option : {Str::SpeedUnitBytes, Str::SpeedUnitBits}) {
        SendMessageW(speedUnit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(option)));
    }
    SendMessageW(speedUnit, CB_SETCURSEL, settings_.speedInBits ? 1 : 0, 0);

    SendDlgItemMessageW(dialog_, IDC_SET_MAX_DOWNLOADS_SPIN, UDM_SETRANGE32, 1, 10);
    SendDlgItemMessageW(dialog_, IDC_SET_MAX_DOWNLOADS_SPIN, UDM_SETPOS32, 0, settings_.maxDownloads);
    SetDlgItemInt(dialog_, IDC_SET_SPEED_LIMIT, static_cast<UINT>(settings_.speedLimitKBps), FALSE);

    CheckDlgButton(dialog_, IDC_SET_SCHEDULE, settings_.scheduleEnabled ? BST_CHECKED : BST_UNCHECKED);
    setTime(IDC_SET_SCHEDULE_START, settings_.scheduleStart);
    setTime(IDC_SET_SCHEDULE_END, settings_.scheduleEnd);
    EnableWindow(GetDlgItem(dialog_, IDC_SET_SCHEDULE_START), settings_.scheduleEnabled);
    EnableWindow(GetDlgItem(dialog_, IDC_SET_SCHEDULE_END), settings_.scheduleEnabled);

    HWND whenDone = GetDlgItem(dialog_, IDC_SET_WHEN_DONE);
    SendMessageW(whenDone, CB_RESETCONTENT, 0, 0);
    for (Str option : {Str::WhenDoneNothing, Str::WhenDoneSleep, Str::WhenDoneShutdown}) {
        SendMessageW(whenDone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(option)));
    }
    SendMessageW(whenDone, CB_SETCURSEL, static_cast<WPARAM>(settings_.whenDone), 0);
    filling_ = false;
}

void SettingsPage::setTime(int controlId, int minutes) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    time.wHour = static_cast<WORD>(minutes / 60);
    time.wMinute = static_cast<WORD>(minutes % 60);
    time.wSecond = 0;
    time.wMilliseconds = 0;
    SendDlgItemMessageW(dialog_, controlId, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&time));
}

void SettingsPage::setUpdateStatus(const std::wstring& text, bool updateReady) {
    updateReady_ = updateReady;
    SetDlgItemTextW(dialog_, IDC_SET_VERSION, text.c_str());
    SetDlgItemTextW(dialog_, IDC_SET_UPDATE, tr(updateReady ? Str::UpdateNow : Str::UpdateCheckNow));
}

int SettingsPage::readTime(int controlId) const {
    SYSTEMTIME time{};
    SendDlgItemMessageW(dialog_, controlId, DTM_GETSYSTEMTIME, 0, reinterpret_cast<LPARAM>(&time));
    return time.wHour * 60 + time.wMinute;
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

void SettingsPage::updateScroll() {
    // Conteúdo: até o controle mais baixo (posição atual + o quanto já rolou), com folga.
    RECT client;
    GetClientRect(dialog_, &client);
    int bottom = 0;
    for (HWND child = GetWindow(dialog_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect;
        GetWindowRect(child, &rect);
        MapWindowPoints(nullptr, dialog_, reinterpret_cast<POINT*>(&rect), 2);
        bottom = std::max<int>(bottom, rect.bottom + scrollPosition_);
    }
    RECT margin{0, 0, 0, 12};
    MapDialogRect(dialog_, &margin);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
    info.nMin = 0;
    info.nMax = bottom + margin.bottom;
    info.nPage = static_cast<UINT>(client.bottom + 1);
    SetScrollInfo(dialog_, SB_VERT, &info, TRUE);
    const int maxPosition = std::max<int>(0, info.nMax - static_cast<int>(client.bottom));
    if (scrollPosition_ > maxPosition) scrollTo(maxPosition);
}

void SettingsPage::scrollTo(int position) {
    RECT client;
    GetClientRect(dialog_, &client);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
    GetScrollInfo(dialog_, SB_VERT, &info);
    const int maxPosition = std::max(0, info.nMax - static_cast<int>(info.nPage) + 1);
    position = std::clamp(position, 0, maxPosition);
    if (position == scrollPosition_) return;
    ScrollWindowEx(dialog_, 0, scrollPosition_ - position, nullptr, nullptr, nullptr, nullptr,
                   SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
    scrollPosition_ = position;
    SCROLLINFO position_info{sizeof(position_info), SIF_POS};
    position_info.nPos = position;
    SetScrollInfo(dialog_, SB_VERT, &position_info, TRUE);
}

INT_PTR SettingsPage::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;
    switch (message) {
        case WM_SIZE:
            updateScroll();
            return FALSE;
        case WM_MOUSEWHEEL: {
            const int lines = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            RECT step{0, 0, 0, 30};
            MapDialogRect(dialog_, &step);
            scrollTo(scrollPosition_ - lines * step.bottom);
            return TRUE;
        }
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(dialog_, SB_VERT, &info);
            int position = scrollPosition_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: position -= 20; break;
                case SB_LINEDOWN: position += 20; break;
                case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: position = info.nTrackPos; break;
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = info.nMax; break;
            }
            scrollTo(position);
            return TRUE;
        }
    }
    if (message == WM_NOTIFY && !filling_) {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->code == DTN_DATETIMECHANGE) {
            settings_.scheduleStart = readTime(IDC_SET_SCHEDULE_START);
            settings_.scheduleEnd = readTime(IDC_SET_SCHEDULE_END);
            notify();
        }
        return FALSE;
    }
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
        case IDC_SET_KEEP_AWAKE:
            settings_.keepAwake = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_ADOPT:
            settings_.adoptBrowserDownloads = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_BROWSER_ASK:
            settings_.askForBrowserDownloads = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_AUTO_UPDATE:
            settings_.autoUpdate = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_BROWSER_FOLDER:
            settings_.browserFolder = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_COMPLETE_WINDOW:
            settings_.showCompleteWindow = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            notify();
            return TRUE;
        case IDC_SET_SPEED_UNIT:
            if (code == CBN_SELCHANGE) {
                settings_.speedInBits = SendDlgItemMessageW(dialog_, IDC_SET_SPEED_UNIT, CB_GETCURSEL, 0, 0) == 1;
                notify();
            }
            return TRUE;
        case IDC_SET_UPDATE:
            if (onUpdateButton) onUpdateButton();
            return TRUE;
        case IDC_SET_DEBRID_CONNECT: {
            if (debridConnected_) {
                if (onDebridDisconnect) onDebridDisconnect();
                return TRUE;
            }
            std::string token = dm::toUtf8(windowText(GetDlgItem(dialog_, IDC_SET_DEBRID_TOKEN)));
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.pop_back();
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.erase(0, 1);
            if (token.empty()) {
                SetFocus(GetDlgItem(dialog_, IDC_SET_DEBRID_TOKEN));
            } else if (onDebridConnect) {
                onDebridConnect(token);
            }
            return TRUE;
        }
        case IDC_SET_MAX_DOWNLOADS:
            if (code == EN_CHANGE) {
                BOOL valid = FALSE;
                const UINT value = GetDlgItemInt(dialog_, IDC_SET_MAX_DOWNLOADS, &valid, FALSE);
                if (valid && value >= 1 && value <= 10 && static_cast<int>(value) != settings_.maxDownloads) {
                    settings_.maxDownloads = static_cast<int>(value);
                    notify();
                }
            }
            return TRUE;
        case IDC_SET_SPEED_LIMIT:
            if (code == EN_CHANGE) {
                BOOL valid = FALSE;
                const UINT value = GetDlgItemInt(dialog_, IDC_SET_SPEED_LIMIT, &valid, FALSE);
                const int64_t limit = valid ? value : 0;
                if (limit != settings_.speedLimitKBps) {
                    settings_.speedLimitKBps = limit;
                    notify();
                }
            }
            return TRUE;
        case IDC_SET_SCHEDULE:
            settings_.scheduleEnabled = IsDlgButtonChecked(dialog_, id) == BST_CHECKED;
            EnableWindow(GetDlgItem(dialog_, IDC_SET_SCHEDULE_START), settings_.scheduleEnabled);
            EnableWindow(GetDlgItem(dialog_, IDC_SET_SCHEDULE_END), settings_.scheduleEnabled);
            notify();
            return TRUE;
        case IDC_SET_WHEN_DONE:
            if (code == CBN_SELCHANGE) {
                const auto selected = SendDlgItemMessageW(dialog_, IDC_SET_WHEN_DONE, CB_GETCURSEL, 0, 0);
                if (selected >= 0) {
                    settings_.whenDone = static_cast<dm::WhenDone>(selected);
                    notify();
                }
            }
            return TRUE;
    }
    return FALSE;
}

}  // namespace ui
