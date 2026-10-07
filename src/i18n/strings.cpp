#include "i18n/strings.h"

#include <windows.h>

#include <array>

namespace i18n {
namespace {

using Table = std::array<const wchar_t*, static_cast<size_t>(Str::Count)>;

constexpr Table kPortuguese = {
    L"Download Manager",
    L"Downloads",
    L"Concluídos",
    L"Regras",
    L"Configurações",
    L"+ Adicionar",
    L"Nome",
    L"Tamanho",
    L"Progresso",
    L"Velocidade",
    L"Tempo restante",
    L"Status",
    L"Pasta",
    L"Concluído em",
    L"Se",
    L"Então",
    L"Em construção.",
};

constexpr Table kEnglish = {
    L"Download Manager",
    L"Downloads",
    L"Completed",
    L"Rules",
    L"Settings",
    L"+ Add",
    L"Name",
    L"Size",
    L"Progress",
    L"Speed",
    L"Time left",
    L"Status",
    L"Folder",
    L"Completed at",
    L"If",
    L"Then",
    L"Under construction.",
};

const Table* g_current = &kPortuguese;

}  // namespace

void setLanguage(Language language) {
    g_current = language == Language::English ? &kEnglish : &kPortuguese;
}

Language systemLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE ? Language::Portuguese
                                                                         : Language::English;
}

const wchar_t* tr(Str id) {
    return (*g_current)[static_cast<size_t>(id)];
}

}  // namespace i18n
