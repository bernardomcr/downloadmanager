#pragma once

// Todo texto visível ao usuário passa por aqui. Para adicionar uma frase:
// 1) crie o identificador em Str, 2) adicione a tradução nas duas tabelas de strings.cpp, na mesma posição.

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
    ErrHttpStatus,  // %lu = status HTTP
    ErrLinkExpired,  // %lu = status HTTP
    ErrServerChanged,
    ErrDiskFull,
    ErrAccessDenied,
    ErrFileSystem,  // %lu = código do Windows
    ErrNetwork,  // %lu = código do Windows
    CliUsage,
    CliPaused,
    CliPausedNotResumable,
    CliCompleted,  // %ls = caminho do arquivo
    StatusConnecting,
    StatusDownloading,
    StatusPaused,
    StatusPausedRestart,
    StatusCompleted,
    MenuResume,
    MenuPause,
    MenuChangeUrl,
    MenuCopyUrl,
    MenuOpen,
    MenuOpenFolder,
    MenuRemove,
    MenuRemoveFromList,
    MenuDeleteFile,
    TrayOpen,
    TrayExit,
    NotifyCompleted,
    AddTitle,
    AddUrlLabel,
    AddFolderLabel,
    AddNameLabel,
    AddStart,
    Browse,
    Cancel,
    Save,
    InvalidUrlMessage,
    ChangeUrlTitle,
    ChangeUrlLabel,
    ConfirmRemove,
    ConfirmDeleteFile,
    ChooseFolder,
    SettingsFolder,
    SettingsConnections,
    SettingsLanguage,
    LanguageAutomatic,
    LanguagePortuguese,
    LanguageEnglish,
    SettingsCloseToTray,
    SettingsStartWithWindows,
    SettingsNotify,
    StatusQueued,
    StatusWaitingSchedule,
    MenuStartNow,
    MenuSpeedLimit,
    SpeedLimitTitle,
    SpeedLimitLabel,
    SettingsMaxDownloads,
    SettingsSpeedLimit,
    SettingsSchedule,
    SettingsScheduleAnd,
    SettingsWhenDone,
    WhenDoneNothing,
    WhenDoneSleep,
    WhenDoneShutdown,
    SettingsKeepAwake,
    CountdownTitle,
    CountdownShutdown,
    CountdownSleep,
    CountdownNow,
    SpeedLimitedSuffix,
    SettingsBrowserAsk,
    SettingsAdopt,
    Count  // manter por último
};

void setLanguage(Language language);
Language currentLanguage();
Language systemLanguage();
const wchar_t* tr(Str id);
// Separador decimal do idioma atual (',' ou '.').
char decimalSeparator();

}  // namespace i18n
