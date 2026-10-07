#pragma once

#include <windows.h>

#include <functional>
#include <vector>

#include "core/rules.h"

namespace ui {

// Aba "Regras": lista de regras SE → ENTÃO (com caixa para ligar/desligar cada uma) e os botões de edição.
class RulesPage {
public:
    HWND create(HWND parent);
    HWND handle() const { return dialog_; }

    void setRules(std::vector<dm::Rule> rules, bool enabled);
    void applyTexts();
    void applyDpi(UINT dpi);

    // Qualquer mudança: regras novas e se a organização está ligada.
    std::function<void(const std::vector<dm::Rule>&, bool)> onChanged;

private:
    static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void fillList();
    void layout();
    int selected() const;
    void select(int index);
    void editRule(int index);  // -1: nova
    void notify();

    HWND dialog_ = nullptr;
    HWND list_ = nullptr;
    std::vector<dm::Rule> rules_;
    bool enabled_ = true;
    bool filling_ = false;
    UINT dpi_ = 96;
};

// Diálogo de edição de uma regra. `rule` entra com os valores atuais e sai com os confirmados.
bool showRuleDialog(HWND owner, dm::Rule& rule);

// Textos de resumo para a lista ("extensão zip, rar · site x.com" / "pasta Compactados · extrair").
std::wstring describeCondition(const dm::Rule& rule);
std::wstring describeAction(const dm::Rule& rule);

}  // namespace ui
