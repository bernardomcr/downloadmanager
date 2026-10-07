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
    L"Conectando…",
    L"Baixando",
    L"Pausado",
    L"Pausado (vai recomeçar do zero)",
    L"Concluído",
    L"Continuar",
    L"Pausar",
    L"Trocar link…",
    L"Copiar link",
    L"Abrir",
    L"Abrir pasta",
    L"Remover",
    L"Remover da lista",
    L"Apagar arquivo",
    L"Abrir Download Manager",
    L"Sair",
    L"Download concluído",
    L"Adicionar download",
    L"Link",
    L"Salvar em",
    L"Nome do arquivo (opcional)",
    L"Baixar",
    L"Procurar…",
    L"Cancelar",
    L"Salvar",
    L"Cole um link que comece com http:// ou https://.",
    L"Trocar link",
    L"Novo link para o mesmo arquivo. O download continua de onde parou.",
    L"Remover os downloads selecionados? Os arquivos incompletos serão apagados.",
    L"Mover os arquivos selecionados para a Lixeira?",
    L"Escolha a pasta",
    L"Pasta padrão dos downloads",
    L"Conexões por download (1 a 32)",
    L"Idioma",
    L"Automático (idioma do Windows)",
    L"Português",
    L"English",
    L"Ao fechar a janela, continuar rodando na bandeja",
    L"Iniciar com o Windows",
    L"Avisar quando um download terminar",
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
    L"Connecting…",
    L"Downloading",
    L"Paused",
    L"Paused (will start over)",
    L"Completed",
    L"Resume",
    L"Pause",
    L"Replace link…",
    L"Copy link",
    L"Open",
    L"Open folder",
    L"Remove",
    L"Remove from list",
    L"Delete file",
    L"Open Download Manager",
    L"Exit",
    L"Download completed",
    L"Add download",
    L"Link",
    L"Save to",
    L"File name (optional)",
    L"Download",
    L"Browse…",
    L"Cancel",
    L"Save",
    L"Paste a link starting with http:// or https://.",
    L"Replace link",
    L"New link to the same file. The download continues where it stopped.",
    L"Remove the selected downloads? Incomplete files will be deleted.",
    L"Move the selected files to the Recycle Bin?",
    L"Choose the folder",
    L"Default download folder",
    L"Connections per download (1 to 32)",
    L"Language",
    L"Automatic (Windows language)",
    L"Português",
    L"English",
    L"Keep running in the tray when the window is closed",
    L"Start with Windows",
    L"Notify when a download finishes",
};

// Falha na compilação se alguma tradução ficou faltando.
constexpr bool complete(const Table& table) {
    for (const wchar_t* text : table) {
        if (text == nullptr) return false;
    }
    return true;
}
static_assert(complete(kPortuguese) && complete(kEnglish), "tradução faltando em strings.cpp");

Language g_language = Language::Portuguese;
const Table* g_current = &kPortuguese;

}  // namespace

void setLanguage(Language language) {
    g_language = language;
    g_current = language == Language::English ? &kEnglish : &kPortuguese;
}

Language currentLanguage() {
    return g_language;
}

Language systemLanguage() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE ? Language::Portuguese
                                                                         : Language::English;
}

const wchar_t* tr(Str id) {
    return (*g_current)[static_cast<size_t>(id)];
}

char decimalSeparator() {
    return g_language == Language::Portuguese ? ',' : '.';
}

}  // namespace i18n
