#pragma once

#include <array>
#include <optional>
#include <string>

namespace dm {

// Versão "1.2.3" (aceita "v" na frente, como nas tags do GitHub).
using Version = std::array<int, 3>;
std::optional<Version> parseVersion(const std::string& text);
std::string formatVersion(const Version& version);

// Nome fixo dos arquivos publicados em cada release.
inline constexpr const char* kSetupAssetName = "DownloadManager-Setup.exe";
inline constexpr const char* kChecksumAssetName = "DownloadManager-Setup.exe.sha256";

struct ReleaseInfo {
    Version version{};
    std::string setupUrl;
    std::string checksumUrl;
};

// Resposta de api.github.com/repos/<dono>/<repo>/releases/latest. Vazio se faltar a versão, o
// instalador ou o .sha256, ou se for rascunho/pré-lançamento. Só aceita links https do GitHub
// (anyHost: servidor de teste local).
std::optional<ReleaseInfo> parseLatestRelease(const std::string& json, bool anyHost = false);

// Conteúdo de um .sha256 ("<64 hex>  nome" ou só o hash). Devolve o hash em minúsculas, ou vazio.
std::string parseChecksumFile(const std::string& text);

}  // namespace dm
