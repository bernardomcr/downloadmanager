#pragma once

#include <windows.h>
#include <commctrl.h>

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "app/download_manager.h"
#include "i18n/strings.h"

namespace ui {

// Lista de downloads (aba Downloads) ou de concluídos (aba Concluídos). ListView virtual:
// os textos vêm direto do DownloadManager a cada pintura, sem copiar dados para o controle.
// Toda célula é desenhada pelo app (ícone do tipo de arquivo, barra de progresso, cores por estado).
class DownloadListView {
public:
    enum class Mode { Active, Completed };

    HWND create(HWND parent, Mode mode, app::DownloadManager& manager);
    HWND handle() const { return list_; }

    // Recalcula quais itens aparecem e redesenha.
    void refresh();
    void applyTexts();
    void applyDpi(UINT dpi);
    // Fonte das linhas (a do cabeçalho é derivada dela).
    void setFont(HFONT font);

    // Repassados pela janela pai. Devolvem true se a mensagem era desta lista.
    bool handleNotify(const NMHDR* header, LRESULT& result);
    bool handleContextMenu(HWND source, POINT screenPoint);

    // Chamado depois de uma ação que muda a lista (pausar, remover...), para atualizar as duas abas.
    std::function<void()> onChanged;
    // "Extrair" na aba Concluídos (o extrair inteligente fica com a janela de concluído).
    std::function<void(uint64_t id)> onExtract;

private:
    struct Column {
        i18n::Str title;
        int width;
        int format;
    };

    static LRESULT CALLBACK listProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
    std::span<const Column> columns() const;
    std::vector<uint64_t> selectedIds() const;
    std::wstring cellText(const app::DownloadItem& item, int column) const;
    COLORREF cellColor(const app::DownloadItem& item, int column) const;
    void drawCell(NMLVCUSTOMDRAW* draw);
    void drawName(HDC dc, const RECT& cell, const app::DownloadItem& item);
    void drawProgress(HDC dc, const RECT& cell, const app::DownloadItem& item);
    LRESULT drawHeader(NMCUSTOMDRAW* draw);
    void fitLastColumn();
    int iconFor(const app::DownloadItem& item);
    void runCommand(int command, const std::vector<uint64_t>& ids);
    void activate(int index);
    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HWND parent_ = nullptr;
    HWND list_ = nullptr;
    Mode mode_ = Mode::Active;
    app::DownloadManager* manager_ = nullptr;
    std::vector<uint64_t> ids_;
    UINT dpi_ = 96;
    HFONT font_ = nullptr;
    HFONT headerFont_ = nullptr;
    HIMAGELIST systemIcons_ = nullptr;
    std::map<std::wstring, int> iconByExtension_;
    bool fitting_ = false;
};

}  // namespace ui
