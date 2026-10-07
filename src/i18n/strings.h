#pragma once

// Todo texto visível ao usuário passa por aqui. Para adicionar uma frase:
// 1) crie o identificador em Str, 2) adicione a tradução nas duas tabelas de strings.cpp.

namespace i18n {

enum class Language { Portuguese, English };

enum class Str {
    AppTitle,
    TabDownloads,
    TabCompleted,
    TabRules,
    TabSettings,
    AddButton,
    ColName,
    ColSize,
    ColProgress,
    ColSpeed,
    ColTimeLeft,
    ColStatus,
    ColFolder,
    ColFinishedAt,
    ColCondition,
    ColAction,
    ComingSoon,
    ErrInvalidUrl,
    ErrNameNotResolved,
    ErrCannotConnect,
    ErrTimeout,
    ErrConnectionLost,
    ErrSecureConnection,
    ErrHttpStatus,      // %lu = status HTTP
    ErrLinkExpired,     // %lu = status HTTP
    ErrServerChanged,
    ErrDiskFull,
    ErrAccessDenied,
    ErrFileSystem,      // %lu = código do Windows
    ErrNetwork,         // %lu = código do Windows
    CliUsage,
    CliPaused,
    CliPausedNotResumable,
    CliCompleted,       // %ls = caminho do arquivo
    Count  // manter por último
};

void setLanguage(Language language);
Language systemLanguage();
const wchar_t* tr(Str id);

}  // namespace i18n
