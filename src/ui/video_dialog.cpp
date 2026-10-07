#include "ui/video_dialog.h"

#include <commctrl.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

#include "app/system.h"
#include "app/video_tools.h"
#include "core/command_line.h"
#include "core/format.h"
#include "engine/process.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

constexpr UINT_PTR kTimer = 1;
constexpr UINT kAnalysisDone = WM_APP + 10;
constexpr int kStandardHeights[] = {2160, 1440, 1080, 720, 480, 360};

enum class Stage { Preparing, Analyzing, Ready, NotFound, Protected, ToolsFailed };

class VideoDialog {
public:
    VideoDialog(app::VideoTools& tools, const VideoRequest& request, VideoChoice& choice)
        : tools_(tools), request_(request), choice_(choice) {}

    ~VideoDialog() {
        process_.kill();
        if (worker_.joinable()) worker_.join();
        if (titleFont_) DeleteObject(titleFont_);
    }

    INT_PTR handle(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;
        switch (message) {
            case WM_INITDIALOG: init(dialog); return FALSE;
            case WM_TIMER: onTimer(); return TRUE;
            case kAnalysisDone: onAnalysisDone(); return TRUE;
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_BROWSE) {
                    const std::wstring folder = app::chooseFolder(dialog_, tr(Str::ChooseFolder),
                                                                  windowText(GetDlgItem(dialog_, IDC_FOLDER)));
                    if (!folder.empty()) SetDlgItemTextW(dialog_, IDC_FOLDER, folder.c_str());
                    return TRUE;
                }
                if (LOWORD(wParam) == IDOK) {
                    if (accept()) EndDialog(dialog_, IDOK);
                    return TRUE;
                }
                if (LOWORD(wParam) == IDCANCEL) {
                    EndDialog(dialog_, IDCANCEL);
                    return TRUE;
                }
                break;
        }
        return FALSE;
    }

private:
    void init(HWND dialog) {
        dialog_ = dialog;
        SetWindowTextW(dialog_, tr(Str::VideoTitle));
        SetDlgItemTextW(dialog_, IDC_VIDEO_NAME, request_.title.empty() ? request_.url.c_str() : request_.title.c_str());
        SetDlgItemTextW(dialog_, IDC_VIDEO_QUALITY_LABEL, tr(Str::VideoQuality));
        SetDlgItemTextW(dialog_, IDC_VIDEO_SUBTITLES, tr(Str::VideoSubtitles));
        SetDlgItemTextW(dialog_, IDC_FOLDER_LABEL, tr(Str::AddFolderLabel));
        SetDlgItemTextW(dialog_, IDC_FOLDER, request_.folder.c_str());
        SetDlgItemTextW(dialog_, IDC_BROWSE, tr(Str::Browse));
        SetDlgItemTextW(dialog_, IDOK, tr(Str::AddStart));
        SetDlgItemTextW(dialog_, IDCANCEL, tr(Str::Cancel));

        // Título em negrito.
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog_, WM_GETFONT, 0, 0));
        LOGFONTW logFont{};
        GetObjectW(font, sizeof(logFont), &logFont);
        logFont.lfWeight = FW_SEMIBOLD;
        titleFont_ = CreateFontIndirectW(&logFont);
        SendDlgItemMessageW(dialog_, IDC_VIDEO_NAME, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);

        HWND list = GetDlgItem(dialog_, IDC_VIDEO_LIST);
        ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        RECT rect{};
        GetClientRect(list, &rect);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(tr(Str::ColName));
        column.cx = rect.right * 78 / 100;
        ListView_InsertColumn(list, 0, &column);
        column.pszText = const_cast<wchar_t*>(tr(Str::ColDuration));
        column.cx = rect.right - column.cx - GetSystemMetrics(SM_CXVSCROLL);
        ListView_InsertColumn(list, 1, &column);

        showPlaylist(false);
        setControlsEnabled(false);
        SetForegroundWindow(dialog_);

        if (tools_.ready()) {
            startAnalysis();
        } else {
            stage_ = Stage::Preparing;
            tools_.install();
            setStatus(tr(Str::VideoPreparing));
        }
        SetTimer(dialog_, kTimer, 200, nullptr);
    }

    void setStatus(const std::wstring& text) {
        SetDlgItemTextW(dialog_, IDC_VIDEO_STATUS, text.c_str());
    }

    void setControlsEnabled(bool enabled) {
        for (int id : {IDC_VIDEO_QUALITY, IDC_VIDEO_SUBTITLES, IDC_VIDEO_LIST, IDOK}) {
            EnableWindow(GetDlgItem(dialog_, id), enabled);
        }
    }

    // Sem playlist, a lista some e o diálogo encolhe.
    void showPlaylist(bool show) {
        ShowWindow(GetDlgItem(dialog_, IDC_VIDEO_PLAYLIST_LABEL), show ? SW_SHOW : SW_HIDE);
        ShowWindow(GetDlgItem(dialog_, IDC_VIDEO_LIST), show ? SW_SHOW : SW_HIDE);
        RECT offset{0, 0, 0, 106};  // de 84 (rótulo) até 190 (pasta), em unidades de diálogo
        MapDialogRect(dialog_, &offset);
        const int delta = show ? offset.bottom - shrunk_ : -(offset.bottom - shrunk_);
        if (show == playlistShown_) return;
        playlistShown_ = show;
        for (int id : {IDC_FOLDER_LABEL, IDC_FOLDER, IDC_BROWSE, IDOK, IDCANCEL}) {
            HWND control = GetDlgItem(dialog_, id);
            RECT rect{};
            GetWindowRect(control, &rect);
            MapWindowPoints(nullptr, dialog_, reinterpret_cast<POINT*>(&rect), 2);
            SetWindowPos(control, nullptr, rect.left, rect.top + delta, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        RECT window{};
        GetWindowRect(dialog_, &window);
        SetWindowPos(dialog_, nullptr, 0, 0, window.right - window.left, window.bottom - window.top + delta,
                     SWP_NOMOVE | SWP_NOZORDER);
    }

    void onTimer() {
        HWND bar = GetDlgItem(dialog_, IDC_VIDEO_PROGRESS);
        if (stage_ == Stage::Preparing) {
            SendMessageW(bar, PBM_SETPOS, static_cast<WPARAM>(tools_.installProgress() * 100), 0);
            if (tools_.state() == app::VideoTools::State::Ready) {
                startAnalysis();
            } else if (tools_.state() == app::VideoTools::State::Failed) {
                stage_ = Stage::ToolsFailed;
                stopProgress();
                setStatus(tr(Str::VideoToolsFailed));
            }
        }
    }

    void stopProgress() {
        HWND bar = GetDlgItem(dialog_, IDC_VIDEO_PROGRESS);
        SetWindowLongPtrW(bar, GWL_STYLE, GetWindowLongPtrW(bar, GWL_STYLE) & ~PBS_MARQUEE);
        SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(bar, SW_HIDE);
    }

    void startAnalysis() {
        stage_ = Stage::Analyzing;
        setStatus(tr(Str::VideoAnalyzing));
        HWND bar = GetDlgItem(dialog_, IDC_VIDEO_PROGRESS);
        SetWindowLongPtrW(bar, GWL_STYLE, GetWindowLongPtrW(bar, GWL_STYLE) | PBS_MARQUEE);
        SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);

        std::string cookies;
        std::string referrer;
        for (const auto& [name, value] : request_.headers) {
            if (name == "Cookie") cookies = value;
            if (name == "Referer") referrer = value;
        }
        const std::string url = dm::toUtf8(request_.url);
        const std::wstring ytDlp = tools_.ytDlpPath();
        const std::wstring tempDirectory = tools_.tempDirectory();
        const std::string userAgent = request_.userAgent;
        worker_ = std::thread([this, url, ytDlp, tempDirectory, cookies, referrer, userAgent] {
            std::wstring cookiesPath;
            if (!cookies.empty()) {
                wchar_t name[MAX_PATH];
                if (GetTempFileNameW(tempDirectory.c_str(), L"dmc", 0, name)) {
                    cookiesPath = name;
                    dm::writeTextFileAtomically(cookiesPath, dm::netscapeCookies(url, cookies));
                }
            }
            const std::string command = dm::buildCommandLine(
                dm::toUtf8(ytDlp), dm::ytDlpAnalyzeArguments(url, dm::toUtf8(cookiesPath), referrer, userAgent));
            std::string json;
            std::string errors;
            if (process_.start(command)) {
                process_.run([&](const std::string& line) {
                    if (!line.empty() && line.front() == '{') json += line;
                    else if (line.rfind("ERROR:", 0) == 0) errors += line + "\n";
                });
            }
            if (!cookiesPath.empty()) DeleteFileW(cookiesPath.c_str());
            {
                std::lock_guard lock(mutex_);
                json_ = std::move(json);
                errors_ = std::move(errors);
            }
            PostMessageW(dialog_, kAnalysisDone, 0, 0);
        });
    }

    void onAnalysisDone() {
        if (worker_.joinable()) worker_.join();
        stopProgress();
        std::string json;
        std::string errors;
        {
            std::lock_guard lock(mutex_);
            json = json_;
            errors = errors_;
        }
        info_ = dm::parseYtDlpInfo(json);
        if ((info_ && info_->drmProtected) || (!info_ && dm::isDrmError(errors))) {
            stage_ = Stage::Protected;
            setStatus(tr(Str::VideoDrm));
            return;
        }
        if (!info_ || (info_->isPlaylist && info_->entries.empty())) {
            // Não é vídeo: oferece baixar o link como arquivo comum.
            stage_ = Stage::NotFound;
            setStatus(tr(Str::VideoNotFound));
            SetDlgItemTextW(dialog_, IDOK, tr(Str::VideoAsFile));
            EnableWindow(GetDlgItem(dialog_, IDOK), TRUE);
            return;
        }

        stage_ = Stage::Ready;
        const std::wstring title = request_.title.empty() ? dm::toWide(info_->title) : request_.title;
        SetDlgItemTextW(dialog_, IDC_VIDEO_NAME, title.c_str());
        setStatus(info_->duration > 0 ? dm::toWide(dm::formatDuration(info_->duration)) : L"");
        fillQualities();
        CheckDlgButton(dialog_, IDC_VIDEO_SUBTITLES, BST_UNCHECKED);

        if (info_->isPlaylist) {
            wchar_t label[128];
            std::swprintf(label, 128, tr(Str::VideoPlaylist), static_cast<int>(info_->entries.size()));
            SetDlgItemTextW(dialog_, IDC_VIDEO_PLAYLIST_LABEL, label);
            HWND list = GetDlgItem(dialog_, IDC_VIDEO_LIST);
            for (int i = 0; i < static_cast<int>(info_->entries.size()); ++i) {
                const auto& entry = info_->entries[i];
                std::wstring name = dm::toWide(entry.title.empty() ? entry.url : entry.title);
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = i;
                item.pszText = name.data();
                ListView_InsertItem(list, &item);
                std::wstring duration = entry.duration > 0 ? dm::toWide(dm::formatDuration(entry.duration)) : L"";
                ListView_SetItemText(list, i, 1, duration.data());
                ListView_SetCheckState(list, i, TRUE);
            }
            showPlaylist(true);
        }
        setControlsEnabled(true);
        EnableWindow(GetDlgItem(dialog_, IDC_VIDEO_SUBTITLES), info_->isPlaylist || info_->hasSubtitles);
        SetFocus(GetDlgItem(dialog_, IDOK));
    }

    void fillQualities() {
        HWND combo = GetDlgItem(dialog_, IDC_VIDEO_QUALITY);
        formats_.clear();
        auto add = [&](const std::wstring& text, dm::VideoFormat format) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            formats_.push_back(format);
        };
        add(tr(Str::QualityBest), {});
        std::vector<int> heights = info_->heights;
        if (info_->isPlaylist) heights.assign(std::begin(kStandardHeights), std::end(kStandardHeights));
        for (size_t i = 0; i < heights.size(); ++i) {
            if (i == 0 && !info_->isPlaylist) continue;  // a maior já é "Melhor qualidade"
            wchar_t text[64];
            std::swprintf(text, 64, tr(Str::QualityHeight), heights[i]);
            add(text, {dm::VideoFormat::Kind::MaxHeight, heights[i]});
        }
        add(tr(Str::QualityMp3), {dm::VideoFormat::Kind::AudioMp3, 0});
        add(tr(Str::QualityAudio), {dm::VideoFormat::Kind::AudioOriginal, 0});
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }

    bool accept() {
        choice_.folder = windowText(GetDlgItem(dialog_, IDC_FOLDER));
        if (stage_ == Stage::NotFound) {
            choice_.downloadAsFile = true;
            return true;
        }
        if (stage_ != Stage::Ready || !info_) return false;

        const auto selected = SendDlgItemMessageW(dialog_, IDC_VIDEO_QUALITY, CB_GETCURSEL, 0, 0);
        choice_.format = selected >= 0 && selected < static_cast<LRESULT>(formats_.size()) ? formats_[selected]
                                                                                          : dm::VideoFormat{};
        choice_.subtitles = IsDlgButtonChecked(dialog_, IDC_VIDEO_SUBTITLES) == BST_CHECKED;
        choice_.items.clear();
        if (info_->isPlaylist) {
            HWND list = GetDlgItem(dialog_, IDC_VIDEO_LIST);
            for (int i = 0; i < static_cast<int>(info_->entries.size()); ++i) {
                if (!ListView_GetCheckState(list, i)) continue;
                choice_.items.push_back({dm::toWide(info_->entries[i].url), dm::toWide(info_->entries[i].title)});
            }
            return !choice_.items.empty();
        }
        choice_.items.push_back({request_.url, request_.title.empty() ? dm::toWide(info_->title) : request_.title});
        return true;
    }

    app::VideoTools& tools_;
    const VideoRequest& request_;
    VideoChoice& choice_;
    HWND dialog_ = nullptr;
    HFONT titleFont_ = nullptr;
    Stage stage_ = Stage::Preparing;
    bool playlistShown_ = true;  // o layout do .rc já reserva espaço para a lista
    int shrunk_ = 0;

    std::thread worker_;
    dm::Process process_;
    std::mutex mutex_;
    std::string json_;
    std::string errors_;
    std::optional<dm::VideoInfo> info_;
    std::vector<dm::VideoFormat> formats_;
};

INT_PTR CALLBACK videoDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, lParam);
    auto* self = reinterpret_cast<VideoDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
    return self ? self->handle(dialog, message, wParam, lParam) : FALSE;
}

}  // namespace

bool showVideoDialog(HWND owner, app::VideoTools& tools, const VideoRequest& request, VideoChoice& choice) {
    VideoDialog dialog(tools, request, choice);
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_VIDEO), owner, videoDialogProc,
                           reinterpret_cast<LPARAM>(&dialog)) == IDOK;
}

}  // namespace ui
