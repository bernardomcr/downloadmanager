#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "app/download_manager.h"
#include "i18n/strings.h"

namespace ui {

// Lista de downloads (aba Downloads) ou de concluídos (aba Concluídos). ListView virtual:
// os textos vêm direto do DownloadManager a cada pintura, sem copiar dados para o controle.
class DownloadListView {
public:
    enum class Mode { Active, Completed };

    HWND create(HWND parent, Mode mode, app::DownloadManager& manager);
    HWND handle() const { return list_; }

    // Recalcula quais itens aparecem e redesenha.
    void refresh();
    void applyTexts();
    void applyDpi(UINT dpi);

    // Repassados pela janela pai. Devolvem true se a mensagem era desta lista.
    bool handleNotify(const NMHDR* header, LRESULT& result);
    bool handleContextMenu(HWND source, POINT screenPoint);

    // Chamado depois de uma ação que muda a lista (pausar, remover...), para atualizar as duas abas.
    std::function<void()> onChanged;

private:
    struct Column {
        i18n::Str title;
        int width;
        int format;
    };

    std::span<const Column> columns() const;
    std::vector<uint64_t> selectedIds() const;
    std::wstring cellText(const app::DownloadItem& item, int column) const;
    void drawProgress(NMLVCUSTOMDRAW* draw);
    void runCommand(int command, const std::vector<uint64_t>& ids);
    void activate(int index);

    HWND parent_ = nullptr;
    HWND list_ = nullptr;
    Mode mode_ = Mode::Active;
    app::DownloadManager* manager_ = nullptr;
    std::vector<uint64_t> ids_;
    UINT dpi_ = 96;
};

}  // namespace ui
