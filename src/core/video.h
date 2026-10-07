#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dm {

// Qualidade escolhida para um vídeo.
struct VideoFormat {
    enum class Kind { Best, MaxHeight, AudioMp3, AudioOriginal };
    Kind kind = Kind::Best;
    int maxHeight = 0;  // para MaxHeight

    std::string serialize() const;                 // "best", "1080", "mp3", "audio"
    static VideoFormat parse(const std::string& text);
};

// Um vídeo (ou item de playlist) para o yt-dlp baixar.
struct VideoJob {
    std::string url;
    std::string outputDirectory;   // UTF-8
    std::string fileNameTemplate;  // vazio: título do vídeo
    VideoFormat format;
    bool subtitles = false;
    std::string ffmpegDirectory;
    std::string cookiesFile;       // arquivo Netscape temporário (vazio = sem cookies)
    std::string referrer;
    std::string userAgent;
    int64_t speedLimit = 0;        // bytes/s
    int concurrentFragments = 8;   // pedaços de HLS/DASH ao mesmo tempo
};

// Argumentos de linha de comando do yt-dlp (sem o executável).
std::vector<std::string> ytDlpArguments(const VideoJob& job);
// Argumentos para analisar um link (título, qualidades, playlist) sem baixar.
std::vector<std::string> ytDlpAnalyzeArguments(const std::string& url, const std::string& cookiesFile,
                                               const std::string& referrer, const std::string& userAgent);

// Linha de progresso emitida pelo modelo de ytDlpArguments.
struct VideoProgress {
    int64_t downloaded = 0;
    int64_t total = -1;
    double speed = 0;
    int64_t eta = -1;
};
std::optional<VideoProgress> parseYtDlpProgress(const std::string& line);
// "DMFILE <caminho>": arquivo final pronto.
std::optional<std::string> parseYtDlpFinalPath(const std::string& line);
// "DMPOST ...": juntando áudio e vídeo / convertendo.
bool isYtDlpPostProcessing(const std::string& line);

// Resultado da análise (-J).
struct VideoInfo {
    struct Entry {
        std::string url;
        std::string title;
        int64_t duration = -1;  // segundos
    };
    std::string title;
    bool isPlaylist = false;
    std::vector<Entry> entries;   // playlist
    std::vector<int> heights;     // resoluções disponíveis, maior primeiro
    bool hasSubtitles = false;
    bool drmProtected = false;
    int64_t duration = -1;
};
std::optional<VideoInfo> parseYtDlpInfo(const std::string& json);

// Mensagem de erro do yt-dlp indica conteúdo com DRM?
bool isDrmError(const std::string& message);

// Página que provavelmente tem vídeo (vale analisar com o yt-dlp em vez de baixar o HTML).
bool looksLikeVideoPage(const std::string& url);

// Arquivo de cookies no formato Netscape (o que o yt-dlp lê), a partir do cabeçalho Cookie.
std::string netscapeCookies(const std::string& url, const std::string& cookieHeader);

}  // namespace dm
