#pragma once

#include <windows.h>

#include <functional>
#include <string>

#include "core/settings.h"

namespace ui {

// Aba "Configurações": um diálogo filho embutido na janela principal. Cada mudança é aplicada na hora.
class SettingsPage {
public:
    HWND create(HWND parent);
    HWND handle() const { return dialog_; }

    void setSettings(const dm::Settings& settings);
    // Reaplica os textos depois de trocar o idioma.
    void applyTexts();

    // Linha da versão/atualização e o texto do botão ("Procurar agora" ou "Atualizar agora").
    void setUpdateStatus(const std::wstring& text, bool updateReady);

    // Real-Debrid: linha de situação e se há conta conectada (o botão vira "Desconectar").
    void setDebridStatus(const std::wstring& text, bool connected, bool checking = false);

    std::function<void(const dm::Settings&)> onChanged;
    std::function<void()> onUpdateButton;
    std::function<void(const std::string& token)> onDebridConnect;
    std::function<void()> onDebridDisconnect;

private:
    static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void fillControls();
    void notify();
    void setTime(int controlId, int minutes);
    int readTime(int controlId) const;
    // A página é mais alta que a janela pequena: rola na vertical.
    void updateScroll();
    void scrollTo(int position);
    int scrollPosition_ = 0;

    HWND dialog_ = nullptr;
    dm::Settings settings_;
    bool filling_ = false;
    bool updateReady_ = false;
    bool debridConnected_ = false;
    bool debridChecking_ = false;
    std::wstring debridStatus_;
    HFONT titleFont_ = nullptr;
};

}  // namespace ui
