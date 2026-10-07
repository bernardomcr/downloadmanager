#include "ui/rules_page.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwchar>
#include <utility>

#include "app/system.h"
#include "core/format.h"
#include "i18n/strings.h"
#include "resource.h"
#include "ui/dialogs.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace ui {
namespace {

constexpr int64_t kMegabyte = 1024 * 1024;

std::wstring format(Str id, const std::wstring& value) {
    wchar_t buffer[512];
    std::swprintf(buffer, 512, tr(id), value.c_str());
    return buffer;
}

void appendPart(std::wstring& text, const std::wstring& part) {
    if (!text.empty()) text += L" · ";
    text += part;
}

std::wstring bytes(int64_t value) {
    return dm::toWide(dm::formatBytes(value, i18n::decimalSeparator()));
}

}  // namespace

std::wstring describeCondition(const dm::Rule& rule) {
    std::wstring text;
    if (rule.kind == dm::Rule::Kind::Video) appendPart(text, tr(Str::DescVideo));
    if (rule.kind == dm::Rule::Kind::File) appendPart(text, tr(Str::DescFile));
    if (!rule.extensions.empty()) appendPart(text, format(Str::DescExtensions, dm::toWide(dm::joinList(rule.extensions))));
    if (!rule.sites.empty()) appendPart(text, format(Str::DescSites, dm::toWide(dm::joinList(rule.sites))));
    if (!rule.nameContains.empty()) appendPart(text, format(Str::DescName, dm::toWide(rule.nameContains)));
    if (rule.minSize > 0) appendPart(text, format(Str::DescMin, bytes(rule.minSize)));
    if (rule.maxSize > 0) appendPart(text, format(Str::DescMax, bytes(rule.maxSize)));
    return text.empty() ? tr(Str::DescAnything) : text;
}

std::wstring describeAction(const dm::Rule& rule) {
    std::wstring text = format(Str::DescFolder, dm::toWide(rule.folder));
    if (rule.extract) appendPart(text, tr(Str::DescExtract));
    if (rule.extract && rule.deleteArchive) appendPart(text, tr(Str::DescDeleteArchive));
    if (rule.openFile) appendPart(text, tr(Str::DescOpenFile));
    if (rule.openFolder) appendPart(text, tr(Str::DescOpenFolder));
    return text;
}

// --- Aba ---

HWND RulesPage::create(HWND parent) {
    dialog_ = CreateDialogParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULES), parent, dialogProc,
                                 reinterpret_cast<LPARAM>(this));
    return dialog_;
}

void RulesPage::setRules(std::vector<dm::Rule> rules, bool enabled) {
    rules_ = std::move(rules);
    enabled_ = enabled;
    CheckDlgButton(dialog_, IDC_RULES_ENABLED, enabled_ ? BST_CHECKED : BST_UNCHECKED);
    fillList();
}

void RulesPage::applyTexts() {
    SetDlgItemTextW(dialog_, IDC_RULES_ENABLED, tr(Str::RulesEnabled));
    SetDlgItemTextW(dialog_, IDC_RULE_NEW, tr(Str::RuleNew));
    SetDlgItemTextW(dialog_, IDC_RULE_EDIT, tr(Str::RuleEdit));
    SetDlgItemTextW(dialog_, IDC_RULE_DELETE, tr(Str::RuleDelete));
    SetDlgItemTextW(dialog_, IDC_RULE_UP, tr(Str::RuleUp));
    SetDlgItemTextW(dialog_, IDC_RULE_DOWN, tr(Str::RuleDown));
    SetDlgItemTextW(dialog_, IDC_RULE_RESTORE, tr(Str::RuleRestore));
    const Str titles[] = {Str::ColRule, Str::ColCondition, Str::ColAction};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = const_cast<wchar_t*>(tr(titles[i]));
        ListView_SetColumn(list_, i, &column);
    }
    fillList();
}

void RulesPage::applyDpi(UINT dpi) {
    dpi_ = dpi;
    layout();
}

void RulesPage::layout() {
    if (!dialog_ || !list_) return;
    RECT client{};
    GetClientRect(dialog_, &client);
    RECT button{};
    GetWindowRect(GetDlgItem(dialog_, IDC_RULE_NEW), &button);
    const int buttonHeight = button.bottom - button.top;
    RECT checkbox{};
    GetWindowRect(GetDlgItem(dialog_, IDC_RULES_ENABLED), &checkbox);
    const int gap = MulDiv(6, static_cast<int>(dpi_), 96);
    const int top = (checkbox.bottom - checkbox.top) + gap;
    const int buttonsTop = client.bottom - buttonHeight;

    MoveWindow(GetDlgItem(dialog_, IDC_RULES_ENABLED), 0, 0, client.right, checkbox.bottom - checkbox.top, TRUE);
    MoveWindow(list_, 0, top, client.right, buttonsTop - top - gap, TRUE);

    // Botões numa linha embaixo; "Restaurar padrão" encostado à direita.
    int x = 0;
    for (int id : {IDC_RULE_NEW, IDC_RULE_EDIT, IDC_RULE_DELETE, IDC_RULE_UP, IDC_RULE_DOWN}) {
        HWND control = GetDlgItem(dialog_, id);
        RECT rect{};
        GetWindowRect(control, &rect);
        MoveWindow(control, x, buttonsTop, rect.right - rect.left, buttonHeight, TRUE);
        x += rect.right - rect.left + gap;
    }
    HWND restore = GetDlgItem(dialog_, IDC_RULE_RESTORE);
    RECT rect{};
    GetWindowRect(restore, &rect);
    MoveWindow(restore, client.right - (rect.right - rect.left), buttonsTop, rect.right - rect.left, buttonHeight, TRUE);

    const int width = client.right - GetSystemMetrics(SM_CXVSCROLL);
    ListView_SetColumnWidth(list_, 0, width * 22 / 100);
    ListView_SetColumnWidth(list_, 1, width * 42 / 100);
    ListView_SetColumnWidth(list_, 2, width - width * 22 / 100 - width * 42 / 100);
}

void RulesPage::fillList() {
    if (!list_) return;
    filling_ = true;
    const int keep = selected();
    ListView_DeleteAllItems(list_);
    for (int i = 0; i < static_cast<int>(rules_.size()); ++i) {
        std::wstring name = dm::toWide(rules_[i].name);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = name.data();
        ListView_InsertItem(list_, &item);
        std::wstring condition = describeCondition(rules_[i]);
        std::wstring action = describeAction(rules_[i]);
        ListView_SetItemText(list_, i, 1, condition.data());
        ListView_SetItemText(list_, i, 2, action.data());
        ListView_SetCheckState(list_, i, rules_[i].enabled);
    }
    if (keep >= 0) select(std::min(keep, static_cast<int>(rules_.size()) - 1));
    filling_ = false;
}

int RulesPage::selected() const {
    return list_ ? ListView_GetNextItem(list_, -1, LVNI_SELECTED) : -1;
}

void RulesPage::select(int index) {
    if (index < 0) return;
    ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_, index, FALSE);
}

void RulesPage::notify() {
    if (onChanged) onChanged(rules_, enabled_);
}

void RulesPage::editRule(int index) {
    dm::Rule rule;
    if (index >= 0) {
        rule = rules_[index];
    } else {
        rule.name = dm::toUtf8(tr(Str::RuleNew));
    }
    if (!showRuleDialog(dialog_, rule)) return;
    if (index >= 0) {
        rules_[index] = rule;
    } else {
        // Regra nova vai para o topo: regras específicas devem vir antes das genéricas.
        rules_.insert(rules_.begin(), rule);
        index = 0;
    }
    fillList();
    select(index);
    notify();
}

INT_PTR CALLBACK RulesPage::dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* self = reinterpret_cast<RulesPage*>(lParam);
        self->dialog_ = dialog;
        self->list_ = GetDlgItem(dialog, IDC_RULES_LIST);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SetWindowTheme(self->list_, L"Explorer", nullptr);
        ListView_SetExtendedListViewStyle(self->list_,
                                          LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.cx = 100;
            column.pszText = const_cast<wchar_t*>(L"");
            ListView_InsertColumn(self->list_, i, &column);
        }
        self->applyTexts();
        return TRUE;
    }
    auto* self = reinterpret_cast<RulesPage*>(GetWindowLongPtrW(dialog, DWLP_USER));
    return self ? self->handleMessage(message, wParam, lParam) : FALSE;
}

INT_PTR RulesPage::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;
    switch (message) {
        case WM_SIZE:
            layout();
            return TRUE;
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->hwndFrom != list_ || filling_) return FALSE;
            if (header->code == LVN_ITEMCHANGED) {
                const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
                // Mudou a caixa de seleção (bits 12+ do estado = imagem de estado).
                if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) &&
                    change->iItem >= 0 && change->iItem < static_cast<int>(rules_.size())) {
                    const bool checked = ListView_GetCheckState(list_, change->iItem) != 0;
                    if (rules_[change->iItem].enabled != checked) {
                        rules_[change->iItem].enabled = checked;
                        notify();
                    }
                }
            } else if (header->code == NM_DBLCLK) {
                const int index = reinterpret_cast<const NMITEMACTIVATE*>(lParam)->iItem;
                if (index >= 0) editRule(index);
            } else if (header->code == LVN_KEYDOWN &&
                       reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey == VK_DELETE) {
                SendMessageW(dialog_, WM_COMMAND, IDC_RULE_DELETE, 0);
            }
            return TRUE;
        }
        case WM_COMMAND: {
            const int index = selected();
            switch (LOWORD(wParam)) {
                case IDC_RULES_ENABLED:
                    enabled_ = IsDlgButtonChecked(dialog_, IDC_RULES_ENABLED) == BST_CHECKED;
                    notify();
                    return TRUE;
                case IDC_RULE_NEW:
                    editRule(-1);
                    return TRUE;
                case IDC_RULE_EDIT:
                    if (index >= 0) editRule(index);
                    return TRUE;
                case IDC_RULE_DELETE:
                    if (index >= 0 && MessageBoxW(dialog_, tr(Str::ConfirmDeleteRule), tr(Str::AppTitle),
                                                  MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        rules_.erase(rules_.begin() + index);
                        fillList();
                        notify();
                    }
                    return TRUE;
                case IDC_RULE_UP:
                case IDC_RULE_DOWN: {
                    const int target = LOWORD(wParam) == IDC_RULE_UP ? index - 1 : index + 1;
                    if (index < 0 || target < 0 || target >= static_cast<int>(rules_.size())) return TRUE;
                    std::swap(rules_[index], rules_[target]);
                    fillList();
                    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED);
                    select(target);
                    notify();
                    return TRUE;
                }
                case IDC_RULE_RESTORE:
                    if (MessageBoxW(dialog_, tr(Str::ConfirmRestoreRules), tr(Str::AppTitle),
                                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        rules_ = dm::defaultRules(i18n::currentLanguage() == i18n::Language::Portuguese);
                        fillList();
                        notify();
                    }
                    return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// --- Diálogo de edição ---

namespace {

std::wstring megabytes(int64_t size) {
    return size > 0 ? std::to_wstring((size + kMegabyte - 1) / kMegabyte) : L"";
}

int64_t readMegabytes(HWND dialog, int id) {
    BOOL valid = FALSE;
    const UINT value = GetDlgItemInt(dialog, id, &valid, FALSE);
    return valid ? static_cast<int64_t>(value) * kMegabyte : 0;
}

INT_PTR CALLBACK ruleDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (const INT_PTR brush = whiteBackground(message, wParam)) return brush;
    switch (message) {
        case WM_INITDIALOG: {
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            const auto* rule = reinterpret_cast<dm::Rule*>(lParam);
            SetWindowTextW(dialog, tr(Str::RuleEditTitle));
            const std::pair<int, Str> labels[] = {
                {IDC_RULE_NAME_LABEL, Str::RuleName},       {IDC_RULE_IF, Str::RuleIf},
                {IDC_RULE_KIND_LABEL, Str::RuleKind},       {IDC_RULE_EXT_LABEL, Str::RuleExtensions},
                {IDC_RULE_SITES_LABEL, Str::RuleSites},     {IDC_RULE_CONTAINS_LABEL, Str::RuleNameContains},
                {IDC_RULE_MIN_LABEL, Str::RuleMinSize},     {IDC_RULE_MAX_LABEL, Str::RuleMaxSize},
                {IDC_RULE_THEN, Str::RuleThen},             {IDC_RULE_FOLDER_LABEL, Str::RuleFolder},
                {IDC_RULE_BROWSE, Str::Browse},             {IDC_RULE_EXTRACT, Str::RuleExtract},
                {IDC_RULE_DELETE_ARCHIVE, Str::RuleDeleteArchive}, {IDC_RULE_OPEN_FILE, Str::RuleOpenFile},
                {IDC_RULE_OPEN_FOLDER, Str::RuleOpenFolder}, {IDOK, Str::Save},
                {IDCANCEL, Str::Cancel},
            };
            for (const auto& [id, text] : labels) SetDlgItemTextW(dialog, id, tr(text));

            // "Se o download…" e "Então…" em negrito.
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0));
            LOGFONTW logFont{};
            GetObjectW(font, sizeof(logFont), &logFont);
            logFont.lfWeight = FW_SEMIBOLD;
            HFONT bold = CreateFontIndirectW(&logFont);
            SetPropW(dialog, L"dm.bold", bold);
            for (int id : {IDC_RULE_IF, IDC_RULE_THEN}) SendDlgItemMessageW(dialog, id, WM_SETFONT, reinterpret_cast<WPARAM>(bold), TRUE);

            SetDlgItemTextW(dialog, IDC_RULE_NAME, dm::toWide(rule->name).c_str());
            HWND kind = GetDlgItem(dialog, IDC_RULE_KIND);
            for (Str option : {Str::KindAny, Str::KindFile, Str::KindVideo}) {
                SendMessageW(kind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tr(option)));
            }
            SendMessageW(kind, CB_SETCURSEL, static_cast<WPARAM>(rule->kind), 0);
            SetDlgItemTextW(dialog, IDC_RULE_EXT, dm::toWide(dm::joinList(rule->extensions)).c_str());
            SetDlgItemTextW(dialog, IDC_RULE_SITES, dm::toWide(dm::joinList(rule->sites)).c_str());
            SetDlgItemTextW(dialog, IDC_RULE_CONTAINS, dm::toWide(rule->nameContains).c_str());
            SetDlgItemTextW(dialog, IDC_RULE_MIN, megabytes(rule->minSize).c_str());
            SetDlgItemTextW(dialog, IDC_RULE_MAX, megabytes(rule->maxSize).c_str());
            SetDlgItemTextW(dialog, IDC_RULE_FOLDER, dm::toWide(rule->folder).c_str());
            CheckDlgButton(dialog, IDC_RULE_EXTRACT, rule->extract ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_RULE_DELETE_ARCHIVE, rule->deleteArchive ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_RULE_OPEN_FILE, rule->openFile ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_RULE_OPEN_FOLDER, rule->openFolder ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(GetDlgItem(dialog, IDC_RULE_DELETE_ARCHIVE), rule->extract);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_RULE_EXTRACT:
                    EnableWindow(GetDlgItem(dialog, IDC_RULE_DELETE_ARCHIVE),
                                 IsDlgButtonChecked(dialog, IDC_RULE_EXTRACT) == BST_CHECKED);
                    return TRUE;
                case IDC_RULE_BROWSE: {
                    const std::wstring folder = app::chooseFolder(dialog, tr(Str::ChooseFolder),
                                                                  windowText(GetDlgItem(dialog, IDC_RULE_FOLDER)));
                    if (!folder.empty()) SetDlgItemTextW(dialog, IDC_RULE_FOLDER, folder.c_str());
                    return TRUE;
                }
                case IDOK: {
                    auto* rule = reinterpret_cast<dm::Rule*>(GetWindowLongPtrW(dialog, DWLP_USER));
                    const std::string folder = dm::toUtf8(windowText(GetDlgItem(dialog, IDC_RULE_FOLDER)));
                    if (folder.find_first_not_of(" ") == std::string::npos) {
                        MessageBoxW(dialog, tr(Str::RuleNeedsFolder), tr(Str::RuleEditTitle), MB_OK | MB_ICONWARNING);
                        SetFocus(GetDlgItem(dialog, IDC_RULE_FOLDER));
                        return TRUE;
                    }
                    rule->name = dm::toUtf8(windowText(GetDlgItem(dialog, IDC_RULE_NAME)));
                    if (rule->name.empty()) rule->name = folder;
                    const auto kind = SendDlgItemMessageW(dialog, IDC_RULE_KIND, CB_GETCURSEL, 0, 0);
                    rule->kind = kind >= 0 ? static_cast<dm::Rule::Kind>(kind) : dm::Rule::Kind::Any;
                    rule->extensions = dm::splitList(dm::toUtf8(windowText(GetDlgItem(dialog, IDC_RULE_EXT))));
                    rule->sites = dm::splitList(dm::toUtf8(windowText(GetDlgItem(dialog, IDC_RULE_SITES))));
                    rule->nameContains = dm::toUtf8(windowText(GetDlgItem(dialog, IDC_RULE_CONTAINS)));
                    rule->minSize = readMegabytes(dialog, IDC_RULE_MIN);
                    rule->maxSize = readMegabytes(dialog, IDC_RULE_MAX);
                    rule->folder = folder;
                    rule->extract = IsDlgButtonChecked(dialog, IDC_RULE_EXTRACT) == BST_CHECKED;
                    rule->deleteArchive = rule->extract && IsDlgButtonChecked(dialog, IDC_RULE_DELETE_ARCHIVE) == BST_CHECKED;
                    rule->openFile = IsDlgButtonChecked(dialog, IDC_RULE_OPEN_FILE) == BST_CHECKED;
                    rule->openFolder = IsDlgButtonChecked(dialog, IDC_RULE_OPEN_FOLDER) == BST_CHECKED;
                    EndDialog(dialog, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(dialog, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_DESTROY:
            if (HFONT bold = static_cast<HFONT>(RemovePropW(dialog, L"dm.bold"))) DeleteObject(bold);
            return FALSE;
    }
    return FALSE;
}

}  // namespace

bool showRuleDialog(HWND owner, dm::Rule& rule) {
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_RULE_EDIT), owner, ruleDialogProc,
                           reinterpret_cast<LPARAM>(&rule)) == IDOK;
}

}  // namespace ui
