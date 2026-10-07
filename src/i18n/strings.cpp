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
    L"Link inválido.",
    L"Endereço não encontrado. Verifique a internet ou o link.",
    L"Não foi possível conectar ao servidor.",
    L"O servidor parou de responder.",
    L"A conexão caiu.",
    L"Falha na conexão segura (certificado).",
    L"O servidor recusou o download (HTTP %lu).",
    L"O link expirou (HTTP %lu). Troque o link para continuar de onde parou.",
    L"O arquivo mudou no servidor. Comece o download de novo.",
    L"Disco cheio.",
    L"Sem permissão para gravar na pasta.",
    L"Erro ao gravar o arquivo (código %lu).",
    L"Erro de rede (código %lu).",
    L"Uso: dm-cli <link> [pasta] [--conexoes N] [--nome arquivo]",
    L"Pausado. Rode o mesmo comando para continuar de onde parou.",
    L"Pausado. Este servidor não permite continuar: o download vai recomeçar do zero.",
    L"Concluído: %ls",
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
    L"Invalid link.",
    L"Address not found. Check your connection or the link.",
    L"Could not connect to the server.",
    L"The server stopped responding.",
    L"The connection dropped.",
    L"Secure connection failed (certificate).",
    L"The server refused the download (HTTP %lu).",
    L"The link expired (HTTP %lu). Replace the link to continue where it stopped.",
    L"The file changed on the server. Start the download again.",
    L"Disk full.",
    L"No permission to write to the folder.",
    L"Error writing the file (code %lu).",
    L"Network error (code %lu).",
    L"Usage: dm-cli <link> [folder] [--connections N] [--name file]",
    L"Paused. Run the same command to continue where it stopped.",
    L"Paused. This server does not allow resuming: the download will start over.",
    L"Completed: %ls",
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
