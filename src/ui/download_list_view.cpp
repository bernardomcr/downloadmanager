#include "ui/download_list_view.h"

#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>

#include "app/system.h"
#include "core/format.h"
#include "core/http_headers.h"
#include "i18n/errors.h"
#include "ui/dialogs.h"
#include "ui/theme.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

constexpr int kNameColumn = 0;
constexpr int kProgressColumn = 2;
constexpr int kStatusColumn = 5;
constexpr int kRowHeight = 38;
constexpr int kCellPadding = 12;

enum Command {
    kResume = 2001,
    kStartNow,
    kSpeedLimit,
    kPause,
    kChangeUrl,
    kCopyUrl,
    kOpen,
    kOpenFolder,
    kRemove,
    kRemoveFromList,
    kDeleteFile,
};

std::wstring displayName(const dm::DownloadRecord& record) {
    if (!record.filePath.empty()) return dm::fileNameOf(dm::toWide(record.filePath));
    if (!record.fileName.empty()) return dm::toWide(record.fileName);
    if (record.debrid) return dm::toWide(record.url.rfind("magnet:", 0) == 0 ? record.url : record.url);
    const std::string fromUrl = dm::fileNameFromUrl(record.url);
    return dm::toWide(fromUrl.empty() ? record.url : fromUrl);
}

std::wstring bytes(int64_t value) {
    return dm::toWide(dm::formatBytes(value, i18n::decimalSeparator()));
}

std::wstring percentText(double fraction) {
    wchar_t text[16];
    std::swprintf(text, 16, fraction >= 0.9995 ? L"%.0f%%" : L"%.1f%%", fraction * 100.0);
    if (wchar_t* dot = wcschr(text, L'.'); dot && i18n::decimalSeparator() == ',') *dot = L',';
    return text;
}

double fractionOf(const dm::DownloadRecord& record) {
    if (record.totalSize <= 0) return record.state == dm::RecordState::Completed ? 1.0 : 0.0;
    return std::clamp(static_cast<double>(record.downloaded) / static_cast<double>(record.totalSize), 0.0, 1.0);
}

std::wstring extensionOf(const std::wstring& name) {
    const size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size() || name.size() - dot > 12) return {};
    std::wstring extension = name.substr(dot + 1);
    for (wchar_t& c : extension) c = static_cast<wchar_t>(std::towlower(c));
    return extension;
}

void drawText(HDC dc, const std::wstring& text, RECT rect, COLORREF color, UINT format) {
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
              format | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

}  // namespace

HWND DownloadListView::create(HWND parent, Mode mode, app::DownloadManager& manager) {
    parent_ = parent;
    mode_ = mode;
    manager_ = &manager;
    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA, 0, 0, 0, 0,
                            parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    // "ItemsView" (Windows 11) não desenha as linhas verticais das colunas; no 10 cai no "Explorer".
    if (FAILED(SetWindowTheme(list_, L"ItemsView", nullptr))) SetWindowTheme(list_, L"Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(list_, theme::kBackground);
    // Sem retângulo pontilhado de foco: a linha selecionada já mostra onde se está.
    SendMessageW(list_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    SetWindowSubclass(list_, listProc, 1, reinterpret_cast<DWORD_PTR>(this));

    SHFILEINFOW info{};
    systemIcons_ = reinterpret_cast<HIMAGELIST>(SHGetFileInfoW(
        L"arquivo", FILE_ATTRIBUTE_NORMAL, &info, sizeof(info), SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES));

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
        {Str::ColName, 236, LVCFMT_LEFT},     {Str::ColSize, 84, LVCFMT_RIGHT},
        {Str::ColProgress, 160, LVCFMT_LEFT}, {Str::ColSpeed, 100, LVCFMT_RIGHT},
        {Str::ColTimeLeft, 116, LVCFMT_RIGHT}, {Str::ColStatus, 120, LVCFMT_LEFT},
    };
    static constexpr Column kCompleted[] = {
        {Str::ColName, 300, LVCFMT_LEFT},
        {Str::ColSize, 84, LVCFMT_RIGHT},
        {Str::ColFolder, 260, LVCFMT_LEFT},
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
    // Linhas altas e arejadas: a altura da linha segue a da lista de imagens (vazia, 1 px de largura).
    HIMAGELIST spacer = ImageList_Create(1, scale(kRowHeight), ILC_COLOR32, 0, 0);
    if (HIMAGELIST old = ListView_SetImageList(list_, spacer, LVSIL_SMALL)) ImageList_Destroy(old);
    const auto cols = columns();
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) ListView_SetColumnWidth(list_, i, scale(cols[i].width));
    if (headerFont_) DeleteObject(headerFont_);
    headerFont_ = theme::createFont(8, FW_SEMIBOLD, dpi_);
    if (HWND header = ListView_GetHeader(list_)) SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(headerFont_), TRUE);
    fitLastColumn();
}

void DownloadListView::setFont(HFONT font) {
    font_ = font;
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (HWND header = ListView_GetHeader(list_); header && headerFont_) {
        SendMessageW(header, WM_SETFONT, reinterpret_cast<WPARAM>(headerFont_), TRUE);
    }
}

void DownloadListView::fitLastColumn() {
    // A última coluna ocupa o resto da largura: a linha selecionada vai de ponta a ponta.
    if (fitting_ || !list_) return;
    fitting_ = true;
    const auto cols = columns();
    const int last = static_cast<int>(cols.size()) - 1;
    RECT client;
    GetClientRect(list_, &client);
    int used = 0;
    for (int i = 0; i < last; ++i) used += ListView_GetColumnWidth(list_, i);
    const int width = std::max<int>(scale(cols[last].width), client.right - used);
    if (ListView_GetColumnWidth(list_, last) != width) ListView_SetColumnWidth(list_, last, width);
    fitting_ = false;
}

LRESULT CALLBACK DownloadListView::listProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR,
                                            DWORD_PTR data) {
    auto* self = reinterpret_cast<DownloadListView*>(data);
    switch (message) {
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom == ListView_GetHeader(hwnd)) {
                if (header->code == NM_CUSTOMDRAW) {
                    return self->drawHeader(reinterpret_cast<NMCUSTOMDRAW*>(lParam));
                }
                if (header->code == HDN_ENDTRACKW || header->code == HDN_ENDTRACKA) {
                    const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
                    PostMessageW(hwnd, WM_SIZE, 0, 0);
                    return result;
                }
            }
            break;
        }
        case WM_SIZE: {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->fitLastColumn();
            return result;
        }
        case WM_PAINT: {
            // O tema do Windows risca as divisões das colunas na área vazia abaixo dos itens: pinta por cima
            // (e escreve a mensagem de lista vazia).
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            RECT client;
            GetClientRect(hwnd, &client);
            RECT header{};
            if (HWND control = ListView_GetHeader(hwnd)) GetWindowRect(control, &header);
            int top = client.top + (header.bottom - header.top);
            const int count = ListView_GetItemCount(hwnd);
            if (count > 0) {
                const int lastVisible =
                    std::min(count - 1, ListView_GetTopIndex(hwnd) + ListView_GetCountPerPage(hwnd));
                RECT last{};
                ListView_GetItemRect(hwnd, lastVisible, &last, LVIR_BOUNDS);
                top = std::max<int>(top, last.bottom);
            }
            if (top < client.bottom) {
                HDC dc = GetDC(hwnd);
                const RECT empty{client.left, top, client.right, client.bottom};
                theme::fillRect(dc, empty, theme::kBackground);
                if (count == 0) {
                    SelectObject(dc, self->font_);
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, theme::kTextTertiary);
                    const wchar_t* text = tr(self->mode_ == Mode::Active ? Str::EmptyDownloads : Str::EmptyCompleted);
                    RECT measure = empty;
                    DrawTextW(dc, text, -1, &measure, DT_CENTER | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
                    RECT box = empty;
                    box.top += (empty.bottom - empty.top - (measure.bottom - measure.top)) / 2 - self->scale(24);
                    DrawTextW(dc, text, -1, &box, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                }
                ReleaseDC(hwnd, dc);
            }
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, listProc, 1);
            if (self->headerFont_) DeleteObject(self->headerFont_);
            self->headerFont_ = nullptr;
            break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT DownloadListView::drawHeader(NMCUSTOMDRAW* draw) {
    HWND header = draw->hdr.hwndFrom;
    switch (draw->dwDrawStage) {
        case CDDS_PREPAINT: {
            RECT client;
            GetClientRect(header, &client);
            theme::fillRect(draw->hdc, client, theme::kBackground);
            return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
        }
        case CDDS_ITEMPREPAINT: {
            const int index = static_cast<int>(draw->dwItemSpec);
            RECT rect = draw->rc;
            theme::fillRect(draw->hdc, rect, theme::kBackground);
            wchar_t text[128] = L"";
            HDITEMW item{};
            item.mask = HDI_TEXT | HDI_FORMAT;
            item.pszText = text;
            item.cchTextMax = 128;
            Header_GetItem(header, index, &item);
            SelectObject(draw->hdc, headerFont_);
            SetBkMode(draw->hdc, TRANSPARENT);
            rect.left += scale(kCellPadding);
            rect.right -= scale(kCellPadding);
            const UINT align = (item.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT;
            drawText(draw->hdc, text, rect, theme::kTextSecondary, align);
            return CDRF_SKIPDEFAULT;
        }
        case CDDS_POSTPAINT: {
            RECT client;
            GetClientRect(header, &client);
            RECT line{client.left, client.bottom - 1, client.right, client.bottom};
            theme::fillRect(draw->hdc, line, theme::kHairline);
            return CDRF_DODEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

int DownloadListView::iconFor(const app::DownloadItem& item) {
    std::wstring extension;
    if (item.record.debrid) {
        extension = L"torrent";
    } else if (item.record.isVideo && item.record.filePath.empty()) {
        extension = item.record.videoFormat == "mp3" ? L"mp3" : L"mp4";
    } else {
        extension = extensionOf(displayName(item.record));
    }
    const auto found = iconByExtension_.find(extension);
    if (found != iconByExtension_.end()) return found->second;
    SHFILEINFOW info{};
    const std::wstring probe = extension.empty() ? L"arquivo" : L"arquivo." + extension;
    SHGetFileInfoW(probe.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                   SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    iconByExtension_[extension] = info.iIcon;
    return info.iIcon;
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
        if (column == 3) return item.organizing ? tr(Str::StatusOrganizing) : app::formatDateTime(record.finishedAt);
        return {};
    }

    switch (column) {
        case kProgressColumn:
            if (record.totalSize > 0) return percentText(fractionOf(record));
            return record.downloaded > 0 ? bytes(record.downloaded) : L"";
        case 3: {
            if (!item.running() || item.live.status != dm::DownloadStatus::Downloading) return {};
            if (item.speed() <= 0) return {};
            return dm::toWide(dm::formatSpeed(item.speed(), i18n::decimalSeparator()));
        }
        case 4:
            if (item.live.remote != dm::RemoteStage::None) return {};
            if (item.running() && item.live.secondsLeft >= 0) return dm::toWide(dm::formatDuration(item.live.secondsLeft));
            if (item.running() && record.totalSize > 0 && item.speed() > 1) {
                const auto seconds =
                    static_cast<int64_t>(static_cast<double>(record.totalSize - record.downloaded) / item.speed());
                return dm::toWide(dm::formatDuration(seconds));
            }
            return {};
        case kStatusColumn:
            if (item.running()) {
                switch (item.live.remote) {
                    case dm::RemoteStage::Preparing: return tr(Str::StatusDebridPreparing);
                    case dm::RemoteStage::Queued: return tr(Str::StatusDebridQueued);
                    case dm::RemoteStage::Downloading: return tr(Str::StatusDebridDownloading);
                    case dm::RemoteStage::Finishing: return tr(Str::StatusDebridFinishing);
                    default: break;
                }
                std::wstring status = tr(item.live.status == dm::DownloadStatus::Connecting ? Str::StatusConnecting
                                                                                            : Str::StatusDownloading);
                if (item.live.postProcessing) {
                    status = tr(Str::StatusProcessing);
                } else if (item.live.part > 1) {
                    wchar_t text[64];
                    std::swprintf(text, 64, tr(Str::StatusVideoPart), item.live.part);
                    status = text;
                }
                if (record.speedLimit > 0) {
                    status += L" · " + std::wstring(tr(Str::SpeedLimitedSuffix)) + L" " +
                              dm::toWide(dm::formatSpeed(static_cast<double>(record.speedLimit),
                                                         i18n::decimalSeparator()));
                }
                return status;
            }
            if (item.queued()) {
                return tr(manager_->scheduleOpen() ? Str::StatusQueued : Str::StatusWaitingSchedule);
            }
            if (record.state == dm::RecordState::Failed) {
                return i18n::describeError(static_cast<dm::DownloadError>(record.errorCode), record.errorDetail,
                                           record.errorText);
            }
            if (item.task && !item.live.resumable && record.downloaded > 0) return tr(Str::StatusPausedRestart);
            return tr(Str::StatusPaused);
    }
    return {};
}

COLORREF DownloadListView::cellColor(const app::DownloadItem& item, int column) const {
    if (column == kNameColumn) return theme::kText;
    if (mode_ == Mode::Active && column == kStatusColumn) {
        if (item.record.state == dm::RecordState::Failed) return theme::kDanger;
        if (item.running()) return item.live.remote != dm::RemoteStage::None ? RGB(109, 40, 217) : theme::kAccent;
        return theme::kTextSecondary;
    }
    if (mode_ == Mode::Completed && column == 3 && item.organizing) return theme::kAccent;
    return theme::kTextSecondary;
}

void DownloadListView::drawName(HDC dc, const RECT& cell, const app::DownloadItem& item) {
    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    const int left = cell.left + scale(kCellPadding);
    const int top = cell.top + (cell.bottom - cell.top - iconSize) / 2;
    if (systemIcons_) ImageList_Draw(systemIcons_, iconFor(item), dc, left, top, ILD_TRANSPARENT);
    RECT text = cell;
    text.left = left + iconSize + scale(10);
    text.right -= scale(8);
    drawText(dc, cellText(item, kNameColumn), text, theme::kText, DT_LEFT);
}

void DownloadListView::drawProgress(HDC dc, const RECT& cell, const app::DownloadItem& item) {
    const dm::DownloadRecord& record = item.record;
    const std::wstring text = cellText(item, kProgressColumn);
    // Porcentagem à direita, em largura fixa (não "dança" enquanto muda); a barra fina ocupa o resto.
    SIZE textSize{};
    GetTextExtentPoint32W(dc, L"100,0%", 6, &textSize);
    const int pad = scale(kCellPadding);
    RECT textRect{cell.right - pad - textSize.cx, cell.top, cell.right - pad, cell.bottom};
    const int barHeight = scale(6);
    const int barTop = cell.top + (cell.bottom - cell.top - barHeight) / 2;
    RECT bar{cell.left + pad, barTop, textRect.left - scale(10), barTop + barHeight};
    if (bar.right - bar.left < scale(24)) {
        bar.right = cell.right - pad;
        textRect = {};
    }

    theme::fillRoundRect(dc, bar, barHeight / 2, theme::kTrack);
    theme::BarColors colors = theme::kBarPaused;
    if (record.state == dm::RecordState::Failed) {
        colors = theme::kBarFailed;
    } else if (item.running()) {
        colors = item.live.remote != dm::RemoteStage::None ? theme::kBarRemote : theme::kBarActive;
    } else if (item.queued()) {
        colors = theme::kBarQueued;
    }
    const double fraction = fractionOf(record);
    if (fraction > 0) {
        RECT filled = bar;
        filled.right = bar.left + std::max<LONG>(barHeight, static_cast<LONG>((bar.right - bar.left) * fraction));
        theme::fillRoundRectGradient(dc, filled, barHeight / 2, colors.from, colors.to);
    }
    if (textRect.right > textRect.left) {
        drawText(dc, text, textRect, item.running() ? theme::kText : theme::kTextSecondary, DT_RIGHT);
    }
}

void DownloadListView::drawCell(NMLVCUSTOMDRAW* draw) {
    const int index = static_cast<int>(draw->nmcd.dwItemSpec);
    const int column = draw->iSubItem;
    HDC dc = draw->nmcd.hdc;
    RECT cell{};
    ListView_GetSubItemRect(list_, index, column, LVIR_BOUNDS, &cell);
    if (column == 0) {
        // A coluna 0 devolve a linha inteira: corta na largura dela.
        cell.right = cell.left + ListView_GetColumnWidth(list_, 0);
    }

    const bool selected = ListView_GetItemState(list_, index, LVIS_SELECTED) != 0;
    const bool hot = ListView_GetHotItem(list_) == index;
    theme::fillRect(dc, cell, selected ? theme::kRowSelected : hot ? theme::kRowHover : theme::kBackground);

    if (index < 0 || index >= static_cast<int>(ids_.size())) return;
    const app::DownloadItem* item = manager_->find(ids_[index]);
    if (!item) return;
    if (font_) SelectObject(dc, font_);
    SetBkMode(dc, TRANSPARENT);

    if (column == kNameColumn) return drawName(dc, cell, *item);
    if (mode_ == Mode::Active && column == kProgressColumn) return drawProgress(dc, cell, *item);

    const auto cols = columns();
    RECT text = cell;
    text.left += scale(kCellPadding);
    text.right -= scale(kCellPadding);
    const UINT align = cols[column].format == LVCFMT_RIGHT ? DT_RIGHT : DT_LEFT;
    drawText(dc, cellText(*item, column), text, cellColor(*item, column),
             align | (mode_ == Mode::Completed && column == 2 ? DT_PATH_ELLIPSIS : 0));
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
            auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(const_cast<NMHDR*>(header));
            switch (draw->nmcd.dwDrawStage) {
                case CDDS_PREPAINT: result = CDRF_NOTIFYITEMDRAW; break;
                case CDDS_ITEMPREPAINT:
                    // O app pinta seleção e destaque (tons suaves); o tema não desenha por cima.
                    draw->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_HOT | CDIS_FOCUS);
                    result = CDRF_NOTIFYSUBITEMDRAW;
                    break;
                case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                    drawCell(draw);
                    result = CDRF_SKIPDEFAULT;
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
    bool anyStopped = false;  // pausado ou com erro
    bool anyQueued = false;
    for (uint64_t id : ids) {
        const app::DownloadItem* item = manager_->find(id);
        if (!item) continue;
        if (item->running()) {
            anyRunning = true;
        } else if (item->queued()) {
            anyQueued = true;
        } else {
            anyStopped = true;
        }
    }
    const bool single = ids.size() == 1;

    HMENU menu = CreatePopupMenu();
    auto add = [&](int command, Str text, bool enabled = true) {
        AppendMenuW(menu, MF_STRING | (enabled ? 0 : MF_GRAYED), command, tr(text));
    };
    if (mode_ == Mode::Active) {
        if (anyStopped) add(kResume, Str::MenuResume);
        if (anyStopped || anyQueued) add(kStartNow, Str::MenuStartNow);
        if (anyRunning || anyQueued) add(kPause, Str::MenuPause);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(kSpeedLimit, Str::MenuSpeedLimit);
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
        runCommand(item->running() || item->queued() ? kPause : kResume, {item->record.id});
    }
}

void DownloadListView::runCommand(int command, const std::vector<uint64_t>& ids) {
    if (ids.empty()) return;
    switch (command) {
        case kResume:
            for (uint64_t id : ids) manager_->resume(id);
            break;
        case kStartNow:
            for (uint64_t id : ids) manager_->startNow(id);
            break;
        case kPause:
            for (uint64_t id : ids) manager_->pause(id);
            break;
        case kSpeedLimit: {
            const app::DownloadItem* first = manager_->find(ids.front());
            if (!first) return;
            int64_t kilobytes = first->record.speedLimit / 1024;
            if (!showSpeedLimitDialog(parent_, kilobytes)) return;
            for (uint64_t id : ids) manager_->setSpeedLimit(id, kilobytes * 1024);
            break;
        }
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
