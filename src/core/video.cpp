#include "core/video.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string_view>

#include "core/http_headers.h"
#include "core/json.h"

namespace dm {
namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string hostOf(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const size_t start = scheme + 3;
    const size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (const size_t at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
    if (const size_t colon = host.find(':'); colon != std::string::npos) host = host.substr(0, colon);
    return lower(host);
}

bool hostMatches(const std::string& host, std::string_view domain) {
    return host == domain || (host.size() > domain.size() && host.ends_with(domain) &&
                              host[host.size() - domain.size() - 1] == '.');
}

std::optional<int64_t> toNumber(const std::string& text) {
    if (text.empty() || text == "NA" || text == "None") return std::nullopt;
    try {
        size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size()) return std::nullopt;
        return static_cast<int64_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

constexpr std::string_view kProgressPrefix = "DMPROG ";
constexpr std::string_view kFilePrefix = "DMFILE ";
constexpr std::string_view kPostPrefix = "DMPOST";

// Ordem de preferência: maior resolução; em empate, formatos que tocam em qualquer lugar (H.264/AAC/MP4).
constexpr const char* kSort = "res,fps,vcodec:h264,acodec:aac,ext:mp4:m4a";

}  // namespace

std::string VideoFormat::serialize() const {
    switch (kind) {
        case Kind::MaxHeight: return std::to_string(maxHeight);
        case Kind::AudioMp3: return "mp3";
        case Kind::AudioOriginal: return "audio";
        default: return "best";
    }
}

VideoFormat VideoFormat::parse(const std::string& text) {
    VideoFormat format;
    if (text == "mp3") {
        format.kind = Kind::AudioMp3;
    } else if (text == "audio") {
        format.kind = Kind::AudioOriginal;
    } else if (const auto height = toNumber(text); height && *height > 0 && *height <= 10000) {
        format.kind = Kind::MaxHeight;
        format.maxHeight = static_cast<int>(*height);
    }
    return format;
}

std::vector<std::string> ytDlpArguments(const VideoJob& job) {
    std::vector<std::string> args = {
        "--newline", "--no-colors", "--encoding", "utf-8", "--no-simulate", "--progress", "--no-playlist",
        "--windows-filenames", "--no-mtime", "--continue", "--part",
        "--concurrent-fragments", std::to_string(std::clamp(job.concurrentFragments, 1, 32)),
        "--progress-template",
        "download:DMPROG %(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|"
        "%(progress.speed)s|%(progress.eta)s",
        "--progress-template", "postprocess:DMPOST %(progress.postprocessor)s",
        "--print", "after_move:DMFILE %(filepath)s",
        "-P", job.outputDirectory,
        "-o", job.fileNameTemplate.empty() ? "%(title).180B.%(ext)s" : job.fileNameTemplate + ".%(ext)s",
    };

    switch (job.format.kind) {
        case VideoFormat::Kind::AudioMp3:
            args.insert(args.end(), {"-f", "ba/b", "-x", "--audio-format", "mp3", "--audio-quality", "0"});
            break;
        case VideoFormat::Kind::AudioOriginal:
            args.insert(args.end(), {"-f", "ba/b", "-x"});
            break;
        case VideoFormat::Kind::MaxHeight: {
            const std::string h = std::to_string(job.format.maxHeight);
            args.insert(args.end(), {"-f", "bv*[height<=" + h + "]+ba/b[height<=" + h + "]/bv*+ba/b", "-S", kSort,
                                     "--merge-output-format", "mp4/mkv"});
            break;
        }
        default:
            args.insert(args.end(), {"-f", "bv*+ba/b", "-S", kSort, "--merge-output-format", "mp4/mkv"});
            break;
    }

    if (job.subtitles) {
        args.insert(args.end(), {"--write-subs", "--sub-langs", "pt.*,en.*", "--embed-subs"});
    }
    if (!job.ffmpegDirectory.empty()) args.insert(args.end(), {"--ffmpeg-location", job.ffmpegDirectory});
    if (!job.cookiesFile.empty()) args.insert(args.end(), {"--cookies", job.cookiesFile});
    if (!job.referrer.empty()) args.insert(args.end(), {"--referer", job.referrer});
    if (!job.userAgent.empty()) args.insert(args.end(), {"--user-agent", job.userAgent});
    if (job.speedLimit > 0) args.insert(args.end(), {"--limit-rate", std::to_string(job.speedLimit)});
    args.insert(args.end(), {"--", job.url});
    return args;
}

std::vector<std::string> ytDlpAnalyzeArguments(const std::string& url, const std::string& cookiesFile,
                                               const std::string& referrer, const std::string& userAgent) {
    std::vector<std::string> args = {"-J", "--flat-playlist", "--no-warnings", "--no-colors", "--encoding", "utf-8"};
    if (!cookiesFile.empty()) args.insert(args.end(), {"--cookies", cookiesFile});
    if (!referrer.empty()) args.insert(args.end(), {"--referer", referrer});
    if (!userAgent.empty()) args.insert(args.end(), {"--user-agent", userAgent});
    args.insert(args.end(), {"--", url});
    return args;
}

std::optional<VideoProgress> parseYtDlpProgress(const std::string& line) {
    const size_t start = line.find(kProgressPrefix);
    if (start == std::string::npos) return std::nullopt;
    std::vector<std::string> fields;
    std::stringstream in(line.substr(start + kProgressPrefix.size()));
    std::string field;
    while (std::getline(in, field, '|')) fields.push_back(field);
    if (fields.size() != 5) return std::nullopt;
    for (auto& f : fields) {
        while (!f.empty() && std::isspace(static_cast<unsigned char>(f.back()))) f.pop_back();
    }

    VideoProgress progress;
    progress.downloaded = toNumber(fields[0]).value_or(0);
    progress.total = toNumber(fields[1]).value_or(toNumber(fields[2]).value_or(-1));
    progress.speed = static_cast<double>(toNumber(fields[3]).value_or(0));
    progress.eta = toNumber(fields[4]).value_or(-1);
    return progress;
}

std::optional<std::string> parseYtDlpFinalPath(const std::string& line) {
    if (line.rfind(kFilePrefix, 0) != 0) return std::nullopt;
    std::string path = line.substr(kFilePrefix.size());
    while (!path.empty() && (path.back() == '\r' || path.back() == '\n')) path.pop_back();
    if (path.empty()) return std::nullopt;
    return path;
}

bool isYtDlpPostProcessing(const std::string& line) {
    return line.rfind(kPostPrefix, 0) == 0;
}

std::optional<VideoInfo> parseYtDlpInfo(const std::string& json) {
    const auto value = parseJson(json);
    if (!value || !value->isObject()) return std::nullopt;

    VideoInfo info;
    info.title = value->string("title");
    info.duration = static_cast<int64_t>(value->number("duration").value_or(-1));
    info.isPlaylist = value->string("_type") == "playlist";

    if (info.isPlaylist) {
        if (const JsonValue* entries = value->field("entries"); entries && entries->array()) {
            for (const JsonValue& entry : *entries->array()) {
                VideoInfo::Entry item;
                item.url = entry.string("url");
                if (item.url.empty()) item.url = entry.string("webpage_url");
                item.title = entry.string("title");
                item.duration = static_cast<int64_t>(entry.number("duration").value_or(-1));
                if (!item.url.empty()) info.entries.push_back(std::move(item));
            }
        }
        return info;
    }

    std::set<int, std::greater<>> heights;
    bool anyFormat = false;
    bool allDrm = true;
    if (const JsonValue* formats = value->field("formats"); formats && formats->array()) {
        for (const JsonValue& format : *formats->array()) {
            anyFormat = true;
            if (format.boolean("has_drm").value_or(false)) continue;
            allDrm = false;
            const auto height = format.number("height");
            if (height && *height > 0 && format.string("vcodec") != "none") heights.insert(static_cast<int>(*height));
        }
    }
    info.drmProtected = anyFormat && allDrm;
    if (const auto height = value->number("height"); height && *height > 0 && heights.empty()) {
        heights.insert(static_cast<int>(*height));
    }
    info.heights.assign(heights.begin(), heights.end());

    for (const char* key : {"subtitles"}) {
        if (const JsonValue* subs = value->field(key); subs && subs->isObject() && subs->serialize() != "{}") {
            info.hasSubtitles = true;
        }
    }
    return info;
}

bool isDrmError(const std::string& message) {
    const std::string text = lower(message);
    return text.find("drm") != std::string::npos;
}

bool looksLikeVideoPage(const std::string& url) {
    static constexpr std::string_view kVideoSites[] = {
        "youtube.com", "youtu.be", "vimeo.com", "dailymotion.com", "twitch.tv", "tiktok.com", "instagram.com",
        "facebook.com", "fb.watch", "x.com", "twitter.com", "reddit.com", "v.redd.it", "soundcloud.com",
        "bilibili.com", "kick.com", "rumble.com", "streamable.com", "bandcamp.com", "globo.com", "globoplay.globo.com",
        "uol.com.br", "pinterest.com", "threads.net", "bsky.app", "odysee.com", "mixcloud.com", "ted.com"};
    const std::string host = hostOf(url);
    if (host.empty()) return false;
    for (const auto site : kVideoSites) {
        if (hostMatches(host, site)) return true;
    }
    // Streams detectados pela extensão.
    const std::string name = lower(fileNameFromUrl(url));
    return name.ends_with(".m3u8") || name.ends_with(".mpd");
}

std::string netscapeCookies(const std::string& url, const std::string& cookieHeader) {
    const std::string host = hostOf(url);
    const bool secure = lower(url).rfind("https://", 0) == 0;
    std::string out = "# Netscape HTTP Cookie File\n";
    std::stringstream in(cookieHeader);
    std::string pair;
    while (std::getline(in, pair, ';')) {
        while (!pair.empty() && pair.front() == ' ') pair.erase(pair.begin());
        const size_t equals = pair.find('=');
        if (equals == std::string::npos || equals == 0 || host.empty()) continue;
        const std::string name = pair.substr(0, equals);
        const std::string value = pair.substr(equals + 1);
        if (name.find_first_of("\t\r\n") != std::string::npos || value.find_first_of("\t\r\n") != std::string::npos) {
            continue;
        }
        // Vale para o domínio e subdomínios (ex.: www.youtube.com e youtube.com).
        const std::string base = host.rfind("www.", 0) == 0 ? host.substr(4) : host;
        out += "." + base + "\tTRUE\t/\t" + (secure ? "TRUE" : "FALSE") + "\t0\t" + name + "\t" + value + "\n";
    }
    return out;
}

}  // namespace dm
