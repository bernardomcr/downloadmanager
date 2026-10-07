#include "ui/download_list_view.h"

#include <uxtheme.h>

#include <algorithm>
#include <cstdio>

#include "app/system.h"
#include "core/format.h"
#include "core/http_headers.h"
#include "i18n/errors.h"
#include "ui/dialogs.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

constexpr int kProgressColumn = 2;

enum Command {
    kResume = 2001,
    kPause,
    kChangeUrl,
    kCopyUrl,
    kOpen,
    kOpenFolder,
    kRemove,
    kRemoveFromList,
    kDeleteFile,
};

constexpr COLORREF kTrack = RGB(229, 231, 235);
constexpr COLORREF kFillActive = RGB(37, 99, 235);
constexpr COLORREF kFillPaused = RGB(156, 163, 175);
constexpr COLORREF kFillFailed = RGB(220, 38, 38);
constexpr COLORREF kSelection = RGB(204, 232, 255);

std::wstring displayName(const dm::DownloadRecord& record) {
    if (!record.filePath.empty()) return dm::fileNameOf(dm::toWide(record.filePath));
    if (!record.fileName.empty()) return dm::toWide(record.fileName);
    const std::string fromUrl = dm::fileNameFromUrl(record.url);
    return dm::toWide(fromUrl.empty() ? record.url : fromUrl);
}

std::wstring bytes(int64_t value) {
    return dm::toWide(dm::formatBytes(value, i18n::decimalSeparator()));
}

std::wstring percentText(double fraction) {
    wchar_t text[16];
    std::swprintf(text, 16, L"%.1f%%", fraction * 100.0);
    if (wchar_t* dot = wcschr(text, L'.'); dot && i18n::decimalSeparator() == ',') *dot = L',';
    return text;
}

double fractionOf(const dm::DownloadRecord& record) {
    if (record.totalSize <= 0) return record.state == dm::RecordState::Completed ? 1.0 : 0.0;
    return std::clamp(static_cast<double>(record.downloaded) / static_cast<double>(record.totalSize), 0.0, 1.0);
}

}  // namespace

HWND DownloadListView::create(HWND parent, Mode mode, app::DownloadManager& manager) {
    parent_ = parent;
    mode_ = mode;
    manager_ = &manager;
    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    const auto cols = columns();
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = cols[i].format;
        column.cx = cols[i].width;
        column.pszText = const_cast<wchar_t*>(tr(cols[i].title));
        ListView_InsertColumn(list_, i, &column);
    }
    refresh();
    return list_;
}

std::span<const DownloadListView::Column> DownloadListView::columns() const {
    static constexpr Column kActive[] = {
        {Str::ColName, 215, LVCFMT_LEFT},     {Str::ColSize, 80, LVCFMT_RIGHT},
        {Str::ColProgress, 110, LVCFMT_LEFT}, {Str::ColSpeed, 85, LVCFMT_RIGHT},
        {Str::ColTimeLeft, 100, LVCFMT_RIGHT}, {Str::ColStatus, 140, LVCFMT_LEFT},
    };
    static constexpr Column kCompleted[] = {
        {Str::ColName, 270, LVCFMT_LEFT},
        {Str::ColSize, 80, LVCFMT_RIGHT},
        {Str::ColFolder, 240, LVCFMT_LEFT},
        {Str::ColFinishedAt, 130, LVCFMT_LEFT},
    };
    if (mode_ == Mode::Active) return kActive;
    return kCompleted;
}

void DownloadListView::refresh() {
    std::vector<const app::DownloadItem*> visible;
    for (const auto& item : manager_->items()) {
        if (item->completed() == (mode_ == Mode::Completed)) visible.push_back(item.get());
    }
    if (mode_ == Mode::Completed) {
        std::stable_sort(visible.begin(), visible.end(), [](const auto* a, const auto* b) {
            return a->record.finishedAt > b->record.finishedAt;
        });
    }

    std::vector<uint64_t> ids;
    ids.reserve(visible.size());
    for (const auto* item : visible) ids.push_back(item->record.id);

    if (ids != ids_) {
        // A seleção é por posição; se a lista mudou, a seleção antiga apontaria para outros itens.
        const bool sameCount = ids.size() == ids_.size();
        ids_ = std::move(ids);
        if (!sameCount) {
            ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemCountEx(list_, static_cast<int>(ids_.size()), LVSICF_NOSCROLL);
        }
    }
    InvalidateRect(list_, nullptr, FALSE);
}

void DownloadListView::applyTexts() {
    const auto cols = columns();
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(tr(cols[i].title));
        ListView_SetColumn(list_, i, &column);
    }
    InvalidateRect(list_, nullptr, FALSE);
}

void DownloadListView::applyDpi(UINT dpi) {
    dpi_ = dpi;
    // Linhas mais altas e arejadas: a altura da linha segue a da lista de imagens (vazia, 1 px de largura).
    HIMAGELIST spacer = ImageList_Create(1, MulDiv(24, static_cast<int>(dpi), 96), ILC_COLOR32, 0, 0);
    if (HIMAGELIST old = ListView_SetImageList(list_, spacer, LVSIL_SMALL)) ImageList_Destroy(old);
    const auto cols = columns();
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        ListView_SetColumnWidth(list_, i, MulDiv(cols[i].width, static_cast<int>(dpi), 96));
    }
}

std::wstring DownloadListView::cellText(const app::DownloadItem& item, int column) const {
    const dm::DownloadRecord& record = item.record;
    if (column == 0) return displayName(record);
    if (column == 1) {
        if (record.totalSize > 0) return bytes(record.totalSize);
        return record.downloaded > 0 ? bytes(record.downloaded) : L"";
    }

    if (mode_ == Mode::Completed) {
        if (column == 2) return dm::directoryOf(dm::toWide(record.filePath));
        if (column == 3) return app::formatDateTime(record.finishedAt);
        return {};
    }

    switch (column) {
        case kProgressColumn:
            return record.totalSize > 0 ? percentText(fractionOf(record)) : bytes(record.downloaded);
        case 3:
            return item.running() && item.live.status == dm::DownloadStatus::Downloading
                       ? dm::toWide(dm::formatSpeed(item.speed(), i18n::decimalSeparator()))
                       : L"";
        case 4:
            if (item.running() && record.totalSize > 0 && item.speed() > 1) {
                const auto seconds =
                    static_cast<int64_t>(static_cast<double>(record.totalSize - record.downloaded) / item.speed());
                return dm::toWide(dm::formatDuration(seconds));
            }
            return {};
        case 5:
            if (item.running()) {
                return tr(item.live.status == dm::DownloadStatus::Connecting ? Str::StatusConnecting
                                                                             : Str::StatusDownloading);
            }
            if (record.state == dm::RecordState::Failed) {
                return i18n::describeError(static_cast<dm::DownloadError>(record.errorCode), record.errorDetail);
            }
            if (item.task && !item.live.resumable && record.downloaded > 0) return tr(Str::StatusPausedRestart);
            return tr(Str::StatusPaused);
    }
    return {};
}

void DownloadListView::drawProgress(NMLVCUSTOMDRAW* draw) {
    const int index = static_cast<int>(draw->nmcd.dwItemSpec);
    if (index < 0 || index >= static_cast<int>(ids_.size())) return;
    const app::DownloadItem* item = manager_->find(ids_[index]);
    if (!item) return;

    HDC dc = draw->nmcd.hdc;
    RECT cell{};
    ListView_GetSubItemRect(list_, index, kProgressColumn, LVIR_BOUNDS, &cell);
    const bool selected = ListView_GetItemState(list_, index, LVIS_SELECTED) != 0;
    HBRUSH background = CreateSolidBrush(selected ? kSelection : RGB(255, 255, 255));
    FillRect(dc, &cell, background);
    DeleteObject(background);

    const int padX = MulDiv(6, static_cast<int>(dpi_), 96);
    const int padY = MulDiv(4, static_cast<int>(dpi_), 96);
    RECT bar{cell.left + padX, cell.top + padY, cell.right - padX, cell.bottom - padY};
    if (bar.right <= bar.left || bar.bottom <= bar.top) return;

    HBRUSH track = CreateSolidBrush(kTrack);
    FillRect(dc, &bar, track);
    DeleteObject(track);

    const double fraction = fractionOf(item->record);
    RECT filled = bar;
    filled.right = bar.left + static_cast<LONG>((bar.right - bar.left) * fraction);
    const COLORREF fillColor = item->record.state == dm::RecordState::Failed ? kFillFailed
                               : item->running()                              ? kFillActive
                                                                              : kFillPaused;
    HBRUSH fill = CreateSolidBrush(fillColor);
    FillRect(dc, &filled, fill);
    DeleteObject(fill);

    // Texto escuro sobre o trilho e branco sobre a parte preenchida.
    const std::wstring text = cellText(*item, kProgressColumn);
    SetBkMode(dc, TRANSPARENT);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, filled.right, bar.top, bar.right, bar.bottom);
    SetTextColor(dc, RGB(31, 41, 55));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(dc, saved);
    const int savedFill = SaveDC(dc);
    IntersectClipRect(dc, filled.left, bar.top, filled.right, bar.bottom);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(dc, savedFill);
}

bool DownloadListView::handleNotify(const NMHDR* header, LRESULT& result) {
    if (header->hwndFrom != list_) return false;
    result = 0;

    switch (header->code) {
        case LVN_GETDISPINFOW: {
            auto* info = reinterpret_cast<NMLVDISPINFOW*>(const_cast<NMHDR*>(header));
            if (!(info->item.mask & LVIF_TEXT)) return true;
            const int index = info->item.iItem;
            const app::DownloadItem* item =
                index >= 0 && index < static_cast<int>(ids_.size()) ? manager_->find(ids_[index]) : nullptr;
            const std::wstring text = item ? cellText(*item, info->item.iSubItem) : L"";
            lstrcpynW(info->item.pszText, text.c_str(), info->item.cchTextMax);
            return true;
        }
        case NM_CUSTOMDRAW: {
            if (mode_ != Mode::Active) {
                result = CDRF_DODEFAULT;
                return true;
            }
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(const_cast<NMHDR*>(header));
            switch (draw->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: result = CDRF_NOTIFYITEMDRAW; break;
                case CDDS_ITEMPREPAINT: result = CDRF_NOTIFYSUBITEMDRAW; break;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                    if (draw->iSubItem == kProgressColumn) {
                        drawProgress(draw);
                        result = CDRF_SKIPDEFAULT;
                    } else {
                        result = CDRF_DODEFAULT;
                    }
                    break;
                default: result = CDRF_DODEFAULT; break;
            }
            return true;
        }
        case NM_DBLCLK:
            activate(reinterpret_cast<const NMITEMACTIVATE*>(header)->iItem);
            return true;
        case LVN_KEYDOWN: {
            const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(header);
            if (key->wVKey == VK_DELETE) {
                runCommand(mode_ == Mode::Active ? kRemove : kRemoveFromList, selectedIds());
            } else if (key->wVKey == VK_RETURN) {
                activate(ListView_GetNextItem(list_, -1, LVNI_FOCUSED));
            } else if (key->wVKey == 'A' && GetKeyState(VK_CONTROL) < 0) {
                ListView_SetItemState(list_, -1, LVIS_SELECTED, LVIS_SELECTED);
            }
            return true;
        }
    }
    return false;
}

bool DownloadListView::handleContextMenu(HWND source, POINT point) {
    if (source != list_) return false;
    const std::vector<uint64_t> ids = selectedIds();
    if (ids.empty()) return true;

    if (point.x == -1 && point.y == -1) {  // menu pelo teclado: abre junto ao item focado
        RECT rect{};
        ListView_GetItemRect(list_, std::max(0, ListView_GetNextItem(list_, -1, LVNI_FOCUSED)), &rect, LVIR_LABEL);
        point = {rect.left, rect.bottom};
        ClientToScreen(list_, &point);
    }

    bool anyRunning = false;
    bool anyStopped = false;
    for (uint64_t id : ids) {
        const app::DownloadItem* item = manager_->find(id);
        if (!item) continue;
        (item->running() ? anyRunning : anyStopped) = true;
    }
    const bool single = ids.size() == 1;

    HMENU menu = CreatePopupMenu();
    auto add = [&](int command, Str text, bool enabled = true) {
        AppendMenuW(menu, MF_STRING | (enabled ? 0 : MF_GRAYED), command, tr(text));
    };
    if (mode_ == Mode::Active) {
        if (anyStopped) add(kResume, Str::MenuResume);
        if (anyRunning) add(kPause, Str::MenuPause);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(kChangeUrl, Str::MenuChangeUrl, single && !anyRunning);
        add(kCopyUrl, Str::MenuCopyUrl);
        add(kOpenFolder, Str::MenuOpenFolder, single);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(kRemove, Str::MenuRemove);
    } else {
        add(kOpen, Str::MenuOpen, single);
        add(kOpenFolder, Str::MenuOpenFolder, single);
        add(kCopyUrl, Str::MenuCopyUrl);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(kRemoveFromList, Str::MenuRemoveFromList);
        add(kDeleteFile, Str::MenuDeleteFile);
    }
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, parent_, nullptr);
    DestroyMenu(menu);
    if (command != 0) runCommand(command, ids);
    return true;
}

std::vector<uint64_t> DownloadListView::selectedIds() const {
    std::vector<uint64_t> ids;
    for (int index = ListView_GetNextItem(list_, -1, LVNI_SELECTED); index >= 0;
         index = ListView_GetNextItem(list_, index, LVNI_SELECTED)) {
        if (index < static_cast<int>(ids_.size())) ids.push_back(ids_[index]);
    }
    return ids;
}

void DownloadListView::activate(int index) {
    if (index < 0 || index >= static_cast<int>(ids_.size())) return;
    const app::DownloadItem* item = manager_->find(ids_[index]);
    if (!item) return;
    if (mode_ == Mode::Completed) {
        runCommand(kOpen, {item->record.id});
    } else {
        runCommand(item->running() ? kPause : kResume, {item->record.id});
    }
}

void DownloadListView::runCommand(int command, const std::vector<uint64_t>& ids) {
    if (ids.empty()) return;
    switch (command) {
        case kResume:
            for (uint64_t id : ids) manager_->resume(id);
            break;
        case kPause:
            for (uint64_t id : ids) manager_->pause(id);
            break;
        case kChangeUrl: {
            const app::DownloadItem* item = manager_->find(ids.front());
            if (!item) return;
            std::wstring url = dm::toWide(item->record.url);
            if (showChangeUrlDialog(parent_, url)) manager_->changeUrl(ids.front(), dm::toUtf8(url));
            break;
        }
        case kCopyUrl: {
            std::wstring text;
            for (uint64_t id : ids) {
                if (const app::DownloadItem* item = manager_->find(id)) {
                    if (!text.empty()) text += L"\r\n";
                    text += dm::toWide(item->record.url);
                }
            }
            app::copyToClipboard(parent_, text);
            return;
        }
        case kOpen:
            if (const app::DownloadItem* item = manager_->find(ids.front())) {
                app::openFile(dm::toWide(item->record.filePath));
            }
            return;
        case kOpenFolder:
            if (const app::DownloadItem* item = manager_->find(ids.front())) {
                if (!item->record.filePath.empty()) {
                    const std::wstring path = dm::toWide(item->record.filePath);
                    app::showInFolder(item->completed() ? path : path + dm::kPartSuffix);
                } else {
                    app::openFile(dm::toWide(item->record.directory));
                }
            }
            return;
        case kRemove:
            if (MessageBoxW(parent_, tr(Str::ConfirmRemove), tr(Str::AppTitle), MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return;
            }
            for (uint64_t id : ids) manager_->remove(id, true);
            break;
        case kRemoveFromList:
            for (uint64_t id : ids) manager_->remove(id, false);
            break;
        case kDeleteFile: {
            if (MessageBoxW(parent_, tr(Str::ConfirmDeleteFile), tr(Str::AppTitle), MB_YESNO | MB_ICONQUESTION) !=
                IDYES) {
                return;
            }
            std::vector<std::wstring> paths;
            for (uint64_t id : ids) {
                if (const app::DownloadItem* item = manager_->find(id)) paths.push_back(dm::toWide(item->record.filePath));
            }
            if (!app::moveToRecycleBin(paths)) return;
            for (uint64_t id : ids) manager_->remove(id, false);
            break;
        }
        default:
            return;
    }
    if (onChanged) onChanged();
}

}  // namespace ui
