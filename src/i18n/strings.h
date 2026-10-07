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
    Count  // manter por último
};

void setLanguage(Language language);
Language systemLanguage();
const wchar_t* tr(Str id);

}  // namespace i18n
