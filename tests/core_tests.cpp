// Testes da parte portátil (roda no Windows e no Linux). Sem framework: cada CHECK que falha
// imprime o local e o processo termina com código 1.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include "core/base64.h"
#include "core/browser_request.h"
#include "core/command_line.h"
#include "core/debrid.h"
#include "core/download_list.h"
#include "core/format.h"
#include "core/http_headers.h"
#include "core/json.h"
#include "core/rate_limiter.h"
#include "core/resume_state.h"
#include "core/rules.h"
#include "core/segments.h"
#include "core/settings.h"
#include "core/update.h"
#include "core/video.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                           \
    do {                                                                           \
        if (!(condition)) {                                                        \
            std::printf("%s:%d: falhou: %s\n", __FILE__, __LINE__, #condition);    \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

void testContentRange() {
    auto range = dm::parseContentRange("bytes 0-99/1000");
    CHECK(range && range->first == 0 && range->last == 99 && range->total == 1000);
    range = dm::parseContentRange("bytes 500-999/*");
    CHECK(range && range->total == -1);
    CHECK(!dm::parseContentRange("bytes */1000"));
    CHECK(!dm::parseContentRange("bytes 10-5/100"));
    CHECK(!dm::parseContentRange("items 0-1/2"));
}

void testFileNames() {
    CHECK(dm::fileNameFromContentDisposition("attachment; filename=\"relatório final.pdf\"") == "relatório final.pdf");
    CHECK(dm::fileNameFromContentDisposition("attachment; filename=plain.zip") == "plain.zip");
    CHECK(dm::fileNameFromContentDisposition(
              "attachment; filename=\"fallback.txt\"; filename*=UTF-8''a%C3%A7%C3%A3o.txt") == "ação.txt");
    CHECK(dm::fileNameFromContentDisposition("inline").empty());

    CHECK(dm::fileNameFromUrl("https://example.com/files/My%20File.iso?token=1#x") == "My File.iso");
    CHECK(dm::fileNameFromUrl("https://example.com/").empty());
    CHECK(dm::fileNameFromUrl("https://example.com").empty());

    CHECK(dm::sanitizeFileName("a<b>c:d.txt") == "a_b_c_d.txt");
    CHECK(dm::sanitizeFileName("CON.txt") == "_CON.txt");
    CHECK(dm::sanitizeFileName("nome. ") == "nome");
    CHECK(dm::sanitizeFileName("") == "download");
    CHECK(dm::sanitizeFileName(std::string(300, 'x') + ".zip").size() <= 180);
    CHECK(dm::sanitizeFileName(std::string(300, 'x') + ".zip").ends_with(".zip"));
}

void testFormat() {
    CHECK(dm::formatBytes(512) == "512 B");
    CHECK(dm::formatBytes(1536) == "1,5 KB");
    CHECK(dm::formatBytes(1536, '.') == "1.5 KB");
    CHECK(dm::formatSpeed(3.0 * 1024 * 1024) == "3,0 MB/s");
    CHECK(dm::formatDuration(9) == "0:09");
    CHECK(dm::formatDuration(3723) == "1:02:03");
    CHECK(dm::formatDuration(-1).empty());
}

void testResumeState() {
    dm::ResumeState state;
    state.url = "https://example.com/a.bin";
    state.fileName = "a.bin";
    state.totalSize = 1000;
    state.etag = "\"abc\"";
    state.segments = {{0, 500, 200, 200, false}, {500, 1000, 1000, 1000, false}};

    const auto parsed = dm::parseResumeState(dm::serializeResumeState(state));
    CHECK(parsed.has_value());
    CHECK(parsed->url == state.url && parsed->totalSize == 1000 && parsed->etag == "\"abc\"");
    CHECK(parsed->segments.size() == 2 && parsed->segments[0].written == 200);

    CHECK(state.matches(1000, "\"abc\"", ""));
    CHECK(!state.matches(1000, "\"other\"", ""));
    CHECK(!state.matches(999, "\"abc\"", ""));
    CHECK(!dm::parseResumeState("lixo"));
    CHECK(!dm::parseResumeState("dmstate 1\nurl x\nsize 10\nsegment 0 20 30\n"));
}

void testSplitting() {
    dm::SegmentPlanner planner(1000, 100);
    const size_t first = planner.acquireFirst();
    planner.commit(first, planner.reserve(first, 100).length);  // 0..100 gravado

    // Segunda conexão divide os 900 restantes ao meio.
    const auto second = planner.acquire();
    CHECK(second.has_value());
    CHECK(planner.end(first) == 550);
    CHECK(planner.position(*second) == 550 && planner.end(*second) == 1000);

    // A primeira conexão não pode passar do novo fim.
    const auto claim = planner.reserve(first, 1000);
    CHECK(claim.offset == 100 && claim.length == 450);
    planner.commit(first, claim.length);
    CHECK(planner.reachedEnd(first));

    // Restos menores que 2x o mínimo não são divididos.
    planner.commit(*second, planner.reserve(*second, 300).length);  // faltam 150
    CHECK(!planner.acquire().has_value());

    planner.commit(*second, planner.reserve(*second, 1000).length);
    CHECK(planner.allComplete());
    CHECK(planner.bytesWritten() == 1000);
}

void testReleaseAndRestore() {
    dm::SegmentPlanner planner(1000, 100);
    const size_t first = planner.acquireFirst();
    planner.commit(first, planner.reserve(first, 300).length);
    planner.reserve(first, 50);  // reservado mas não gravado (ex.: erro de disco)
    planner.release(first);

    auto saved = planner.snapshot();
    CHECK(saved[0].reserved == 300 && !saved[0].owned);

    dm::SegmentPlanner resumed(1000, 100);
    resumed.restore(saved);
    const auto again = resumed.acquire();
    CHECK(again && *again == 0 && resumed.position(0) == 300);
    CHECK(resumed.bytesWritten() == 300);
}

// Várias "conexões" em paralelo: todo byte tem que ser gravado exatamente uma vez.
void testConcurrentCoverage() {
    constexpr int64_t kSize = 5'000'003;
    dm::SegmentPlanner planner(kSize, 4096);
    std::vector<std::atomic<uint8_t>> hits(kSize);

    auto worker = [&](std::optional<size_t> index, unsigned seed) {
        std::mt19937 random(seed);
        while (index) {
            for (;;) {
                const auto claim = planner.reserve(*index, 1 + random() % 70000);
                for (int64_t i = 0; i < claim.length; ++i) hits[claim.offset + i]++;
                planner.commit(*index, claim.length);
                if (claim.length == 0 || planner.reachedEnd(*index)) break;
            }
            index = planner.acquire();
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(worker, planner.acquireFirst(), 1u);
    for (unsigned i = 2; i <= 16; ++i) threads.emplace_back(worker, planner.acquire(), i);
    for (auto& thread : threads) thread.join();

    bool exactlyOnce = true;
    for (const auto& hit : hits) exactlyOnce = exactlyOnce && hit == 1;
    CHECK(exactlyOnce);
    CHECK(planner.allComplete());
    CHECK(planner.bytesWritten() == kSize);
}

void testDownloadList() {
    dm::DownloadRecord record;
    record.id = 7;
    record.url = "https://example.com/a.zip";
    record.directory = "C:\\Users\\Zé\\Downloads";
    record.fileName = "a.zip";
    record.filePath = "C:\\Users\\Zé\\Downloads\\a.zip";
    record.state = dm::RecordState::Completed;
    record.totalSize = 1234;
    record.downloaded = 1234;
    record.addedAt = 1700000000;
    record.finishedAt = 1700000100;
    record.connections = 4;
    record.speedLimit = 51200;
    record.protectedHeaders = "QUJD";
    record.userAgent = "Mozilla/5.0 (X)";
    record.errorCode = 3;
    record.errorDetail = 404;

    dm::DownloadRecord second = record;
    second.id = 8;
    second.isVideo = true;
    second.videoFormat = "720";
    second.subtitles = true;
    second.errorText = "Video unavailable";
    second.organize = true;
    second.state = dm::RecordState::Queued;

    dm::DownloadRecord torrent = record;
    torrent.id = 9;
    torrent.url = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567";
    torrent.debrid = true;
    dm::DownloadRecord sent = torrent;
    sent.id = 10;
    sent.debridId = "ABC123";
    const auto torrents = dm::parseDownloadList(dm::serializeDownloadList({torrent, sent}));
    CHECK(torrents.size() == 2 && torrents[0].debrid && torrents[0].debridId.empty() && torrents[1].debrid &&
          torrents[1].debridId == "ABC123" && torrents[0].url == torrent.url);

    const auto parsed = dm::parseDownloadList(dm::serializeDownloadList({record, second}));
    CHECK(parsed.size() == 2);
    CHECK(!parsed[0].debrid);
    CHECK(parsed[0].id == 7 && parsed[0].directory == record.directory && parsed[0].filePath == record.filePath);
    CHECK(parsed[0].state == dm::RecordState::Completed && parsed[0].totalSize == 1234);
    CHECK(parsed[0].finishedAt == 1700000100 && parsed[0].connections == 4);
    CHECK(parsed[0].errorCode == 3 && parsed[0].errorDetail == 404);
    CHECK(parsed[1].state == dm::RecordState::Queued && parsed[0].speedLimit == 51200);
    CHECK(parsed[0].protectedHeaders == "QUJD" && parsed[0].userAgent == "Mozilla/5.0 (X)");
    CHECK(!parsed[0].isVideo && parsed[1].isVideo && parsed[1].videoFormat == "720" && parsed[1].subtitles);
    CHECK(parsed[1].errorText == "Video unavailable" && parsed[1].organize && !parsed[0].organize);

    // Item sem URL é descartado; o resto continua.
    CHECK(dm::parseDownloadList("dmlist 1\n[download]\nid 1\n[download]\nid 2\nurl x\n").size() == 1);
    CHECK(dm::parseDownloadList("outra coisa").empty());
}

void testSettings() {
    dm::Settings settings;
    settings.downloadFolder = "D:\\Baixados";
    settings.connections = 16;
    settings.language = dm::LanguageSetting::English;
    settings.closeToTray = false;
    settings.maxDownloads = 5;
    settings.speedLimitKBps = 300;
    settings.scheduleEnabled = true;
    settings.scheduleStart = 23 * 60;
    settings.scheduleEnd = 6 * 60 + 30;
    settings.whenDone = dm::WhenDone::Shutdown;
    settings.rulesEnabled = false;
    settings.autoUpdate = false;
    const auto parsed = dm::parseSettings(dm::serializeSettings(settings));
    CHECK(parsed.downloadFolder == "D:\\Baixados" && parsed.connections == 16);
    CHECK(parsed.language == dm::LanguageSetting::English && !parsed.closeToTray);
    CHECK(parsed.startWithWindows && parsed.notifyOnComplete && parsed.keepAwake);
    CHECK(parsed.maxDownloads == 5 && parsed.speedLimitKBps == 300 && parsed.scheduleEnabled);
    CHECK(parsed.scheduleStart == 23 * 60 && parsed.scheduleEnd == 6 * 60 + 30);
    CHECK(parsed.whenDone == dm::WhenDone::Nothing);  // não é salvo
    CHECK(!parsed.rulesEnabled && dm::parseSettings("").rulesEnabled);
    CHECK(!parsed.autoUpdate && dm::parseSettings("").autoUpdate);
    CHECK(dm::parseSettings("connections=500\n").connections == 32);
    CHECK(dm::parseSettings("").connections == 8);
}

void testRateLimiter() {
    using namespace std::chrono;
    dm::RateLimiter limiter;
    const auto start = dm::RateLimiter::Clock::time_point{} + hours(1);
    CHECK(limiter.consume(1'000'000, start) == nanoseconds::zero());  // sem limite

    limiter.setRate(100'000);  // 100 KB/s, rajada de 25 KB
    CHECK(limiter.consume(25'000, start) == nanoseconds::zero());
    // Mais 50 KB sem esperar: falta 0,5 s de fichas.
    CHECK(duration_cast<milliseconds>(limiter.consume(50'000, start)).count() == 500);
    // Passado 1 s, recuperou 100 KB mas a rajada máxima é 25 KB: saldo -50 + 25 + ... limitado.
    const auto wait = limiter.consume(10'000, start + seconds(1));
    CHECK(wait == nanoseconds::zero());

    // Ao longo de 10 s, a vazão média fica no limite.
    dm::RateLimiter steady;
    steady.setRate(200'000);
    auto now = start;
    int64_t total = 0;
    while (now < start + seconds(10)) {
        total += 16'384;
        now += steady.consume(16'384, now) + milliseconds(1);
    }
    CHECK(total > 1'900'000 && total < 2'200'000);
}

void testSchedule() {
    CHECK(dm::scheduleAllows(false, 0, 0, 123));
    CHECK(dm::scheduleAllows(true, 2 * 60, 8 * 60, 3 * 60));
    CHECK(!dm::scheduleAllows(true, 2 * 60, 8 * 60, 8 * 60));
    CHECK(!dm::scheduleAllows(true, 2 * 60, 8 * 60, 12 * 60));
    // Atravessando a meia-noite: 23:00 às 07:00.
    CHECK(dm::scheduleAllows(true, 23 * 60, 7 * 60, 23 * 60 + 30));
    CHECK(dm::scheduleAllows(true, 23 * 60, 7 * 60, 60));
    CHECK(!dm::scheduleAllows(true, 23 * 60, 7 * 60, 12 * 60));
    CHECK(dm::scheduleAllows(true, 5 * 60, 5 * 60, 12 * 60));

    CHECK(dm::parseTimeOfDay("07:30") == 450);
    CHECK(dm::parseTimeOfDay("24:00") == -1);
    CHECK(dm::parseTimeOfDay("abc") == -1);
    CHECK(dm::formatTimeOfDay(450) == "07:30");
}

void testJson() {
    const auto value = dm::parseJson(R"({"a":"x\u00e7\ud83d\ude00","n":-1.5e2,"t":true,"l":[1,{"b":null}],"e":"\"\n"})");
    CHECK(value.has_value());
    CHECK(value->string("a") == "x\xC3\xA7\xF0\x9F\x98\x80");  // ç e 😀 em UTF-8
    CHECK(value->number("n") == -150.0);
    CHECK(value->boolean("t") == true);
    CHECK(value->field("l") && value->field("l")->array() && value->field("l")->array()->size() == 2);
    CHECK(value->string("e") == "\"\n");
    CHECK(value->string("missing").empty() && !value->number("a"));

    // Ida e volta.
    const auto again = dm::parseJson(value->serialize());
    CHECK(again && again->string("a") == value->string("a") && again->string("e") == "\"\n");

    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1} x", "\"\\x\"", "nul", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"}) {
        CHECK(!dm::parseJson(bad).has_value());
    }
}

void testBrowserRequest() {
    dm::BrowserRequest request;
    request.url = "https://example.com/a.zip";
    request.fileName = "a.zip";
    request.referrer = "https://example.com/page";
    request.cookies = "sid=1; x=2";
    request.userAgent = "Mozilla/5.0";
    request.source = dm::BrowserRequest::Source::Link;

    const auto parsed = dm::parseBrowserRequest(dm::serializeBrowserRequest(request));
    CHECK(parsed && parsed->url == request.url && parsed->cookies == request.cookies);
    CHECK(parsed->source == dm::BrowserRequest::Source::Link && parsed->referrer == request.referrer);
    const auto headers = dm::browserHeaders(*parsed);
    CHECK(headers.size() == 2 && headers[0].first == "Cookie" && headers[1].first == "Referer");

    CHECK(dm::parseBrowserRequest(R"({"type":"add","url":"https://youtu.be/x","source":"page"})")->source ==
          dm::BrowserRequest::Source::Page);
    CHECK(!dm::parseBrowserRequest(R"({"type":"add","url":"file:///C:/Windows/win.ini"})"));
    CHECK(!dm::parseBrowserRequest(R"({"type":"add","url":"javascript:void"})"));
    // Magnet: só pelo clique direito (source link) e bem formado.
    const std::string magnetJson =
        R"({"type":"add","url":"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=x","source":"link"})";
    CHECK(dm::parseBrowserRequest(magnetJson));
    CHECK(!dm::parseBrowserRequest(R"({"type":"add","url":"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"})"));
    CHECK(!dm::parseBrowserRequest(R"({"type":"add","url":"magnet:?xt=urn:btih:zz","source":"link"})"));
    CHECK(!dm::parseBrowserRequest(R"({"type":"other","url":"https://a.com/x"})"));
    // Cabeçalho com quebra de linha (tentativa de injeção) é descartado; o resto continua valendo.
    const auto injected = dm::parseBrowserRequest(R"({"type":"add","url":"https://a.com/x","cookies":"a=1\r\nX-Evil: 1"})");
    CHECK(injected && injected->cookies.empty());
    const auto badReferrer = dm::parseBrowserRequest(R"({"type":"add","url":"https://a.com/x","referrer":"file:///x"})");
    CHECK(badReferrer && badReferrer->referrer.empty());

    // Cabeçalhos extras: os de controle e os malformados são descartados; Cookie da lista vale.
    const auto extra = dm::parseBrowserRequest(
        R"({"type":"add","url":"https://a.com/x","token":"t-1","headers":[)"
        R"({"name":"Authorization","value":"Bearer abc"},{"name":"Host","value":"evil"},)"
        R"({"name":"Range","value":"bytes=5-"},{"name":"Bad Name","value":"x"},)"
        R"({"name":"X-Evil","value":"a\r\nb: c"},{"name":"Cookie","value":"sid=9"}]})");
    CHECK(extra && extra->token == "t-1" && extra->cookies == "sid=9");
    CHECK(extra->headers.size() == 1 && extra->headers[0].first == "Authorization");
    const auto all = dm::browserHeaders(*extra);
    CHECK(all.size() == 2 && all[0].first == "Cookie" && all[1].first == "Authorization");
    const auto roundTrip = dm::parseBrowserRequest(dm::serializeBrowserRequest(*extra));
    CHECK(roundTrip && roundTrip->headers == extra->headers && roundTrip->token == "t-1");

    const auto adopt = dm::parseAdoptRequest(R"({"type":"adopt","path":"C:\\Users\\a\\Downloads\\x.zip","url":"https://a.com/x"})");
    CHECK(adopt && adopt->path == "C:\\Users\\a\\Downloads\\x.zip" && adopt->url == "https://a.com/x");
    CHECK(adopt && dm::parseAdoptRequest(dm::serializeAdoptRequest(*adopt)).has_value());
    CHECK(dm::parseAdoptRequest(R"({"type":"adopt","path":"\\\\server\\share\\x.zip"})").has_value());
    CHECK(!dm::parseAdoptRequest(R"({"type":"adopt","path":"x.zip"})"));
    CHECK(!dm::parseAdoptRequest(R"({"type":"adopt","path":"C:\\a\\..\\Windows\\x"})"));
    CHECK(!dm::parseAdoptRequest(R"({"type":"adopt","path":"\\\\?\\C:\\x"})"));
    CHECK(!dm::parseAdoptRequest(R"({"type":"add","path":"C:\\x"})"));
}

void testBase64() {
    CHECK(dm::base64Encode("") == "");
    CHECK(dm::base64Encode("f") == "Zg==");
    CHECK(dm::base64Encode("fo") == "Zm8=");
    CHECK(dm::base64Encode("foo") == "Zm9v");
    CHECK(dm::base64Decode("Zm9vYmFy") == std::optional<std::string>("foobar"));
    CHECK(dm::base64Decode("Zg==") == std::optional<std::string>("f"));
    std::string binary;
    for (int i = 0; i < 256; ++i) binary += static_cast<char>(i);
    CHECK(dm::base64Decode(dm::base64Encode(binary)) == std::optional<std::string>(binary));
    CHECK(!dm::base64Decode("Zg="));
    CHECK(!dm::base64Decode("Z=g="));
    CHECK(!dm::base64Decode("****"));
}

void testVideo() {
    CHECK(dm::VideoFormat::parse("1080").kind == dm::VideoFormat::Kind::MaxHeight);
    CHECK(dm::VideoFormat::parse("1080").maxHeight == 1080);
    CHECK(dm::VideoFormat::parse("mp3").serialize() == "mp3");
    CHECK(dm::VideoFormat::parse("lixo").kind == dm::VideoFormat::Kind::Best);

    dm::VideoJob job;
    job.url = "https://www.youtube.com/watch?v=abc";
    job.outputDirectory = "C:\\Vídeos";
    job.format = dm::VideoFormat::parse("720");
    job.subtitles = true;
    job.cookiesFile = "C:\\t\\c.txt";
    job.speedLimit = 1000;
    const auto args = dm::ytDlpArguments(job);
    auto has = [&](const std::string& a, const std::string& b) {
        for (size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == a && args[i + 1] == b) return true;
        }
        return false;
    };
    CHECK(has("-P", "C:\\Vídeos") && has("--cookies", "C:\\t\\c.txt") && has("--limit-rate", "1000"));
    CHECK(has("-f", "bv*[height<=720]+ba/b[height<=720]/bv*+ba/b"));
    CHECK(has("--sub-langs", "pt.*,en.*"));
    CHECK(args.back() == job.url && args[args.size() - 2] == "--");  // link nunca vira opção

    job.format = dm::VideoFormat::parse("mp3");
    const auto audio = dm::ytDlpArguments(job);
    CHECK(std::find(audio.begin(), audio.end(), "--audio-format") != audio.end());

    auto progress = dm::parseYtDlpProgress("DMPROG 1048576|NA|10485760.5|524288.0|18");
    CHECK(progress && progress->downloaded == 1048576 && progress->total == 10485760 && progress->eta == 18);
    progress = dm::parseYtDlpProgress("DMPROG 10|20|NA|NA|NA\r");
    CHECK(progress && progress->total == 20 && progress->speed == 0 && progress->eta == -1);
    CHECK(!dm::parseYtDlpProgress("[download] 10% of 1MiB"));
    CHECK(dm::parseYtDlpFinalPath("DMFILE C:\\x\\Vídeo.mp4\r") == std::optional<std::string>("C:\\x\\Vídeo.mp4"));
    CHECK(dm::isYtDlpPostProcessing("DMPOST Merger"));

    const auto info = dm::parseYtDlpInfo(R"({"title":"Clipe","duration":12.5,"formats":[)"
                                         R"({"height":720,"vcodec":"avc1"},{"height":1080,"vcodec":"vp9"},)"
                                         R"({"height":null,"vcodec":"none"},{"height":2160,"vcodec":"av01","has_drm":true}],)"
                                         R"("subtitles":{"pt":[{"ext":"vtt"}]}})");
    CHECK(info && info->title == "Clipe" && !info->isPlaylist && info->duration == 12);
    CHECK(info->heights == std::vector<int>({1080, 720}) && info->hasSubtitles && !info->drmProtected);
    const auto drm = dm::parseYtDlpInfo(R"({"title":"x","formats":[{"height":720,"has_drm":true}]})");
    CHECK(drm && drm->drmProtected && drm->heights.empty());
    const auto list = dm::parseYtDlpInfo(R"({"_type":"playlist","title":"Lista","entries":[)"
                                         R"({"url":"https://a/1","title":"Um"},{"title":"sem link"},{"url":"https://a/2"}]})");
    CHECK(list && list->isPlaylist && list->entries.size() == 2 && list->entries[0].title == "Um");
    CHECK(dm::isDrmError("ERROR: [Netflix] This video is DRM protected"));

    CHECK(dm::looksLikeVideoPage("https://www.youtube.com/watch?v=1"));
    CHECK(dm::looksLikeVideoPage("https://m.youtube.com/shorts/1"));
    CHECK(dm::looksLikeVideoPage("https://cdn.site.com/live/master.m3u8?token=1"));
    CHECK(!dm::looksLikeVideoPage("https://notyoutube.com/x"));
    CHECK(!dm::looksLikeVideoPage("https://example.com/file.zip"));

    const std::string cookies = dm::netscapeCookies("https://www.youtube.com/watch?v=1", "SID=abc; PREF=f1=1; bad");
    CHECK(cookies.find(".youtube.com\tTRUE\t/\tTRUE\t0\tSID\tabc\n") != std::string::npos);
    CHECK(cookies.find("PREF\tf1=1") != std::string::npos && cookies.find("bad") == std::string::npos);
}

void testCommandLine() {
    CHECK(dm::quoteArgument("simples") == "simples");
    CHECK(dm::quoteArgument("") == "\"\"");
    CHECK(dm::quoteArgument("com espaço") == "\"com espaço\"");
    CHECK(dm::quoteArgument("C:\\Pasta Nova\\") == "\"C:\\Pasta Nova\\\\\"");
    CHECK(dm::quoteArgument("a\"b") == "\"a\\\"b\"");
    CHECK(dm::quoteArgument("a\\\"b") == "\"a\\\\\\\"b\"");
    CHECK(dm::quoteArgument("C:\\sem\\espaco") == "C:\\sem\\espaco");
    CHECK(dm::buildCommandLine("C:\\x y\\yt-dlp.exe", {"-o", "%(title)s.%(ext)s", "--", "https://a/b?c=1&d=2"}) ==
          "\"C:\\x y\\yt-dlp.exe\" -o %(title)s.%(ext)s -- https://a/b?c=1&d=2");
}

void testRules() {
    auto rules = dm::defaultRules(true);
    auto folderFor = [&](const dm::DownloadFacts& facts) {
        const dm::Rule* rule = dm::matchRule(rules, facts);
        return rule ? rule->folder : std::string("(nenhuma)");
    };
    CHECK(folderFor({"https://a.com/x", "Setup.EXE", 10, false}) == "Programas");
    CHECK(folderFor({"https://a.com/x", "fotos.tar.gz", 10, false}) == "Compactados");
    CHECK(folderFor({"https://a.com/x", "relatório.pdf", 10, false}) == "Documentos");
    CHECK(folderFor({"https://youtube.com/x", "clipe.webm", 10, true}) == "Vídeos");  // vídeo vem antes
    CHECK(folderFor({"https://a.com/x", "filme.mkv", 10, false}) == "Vídeos");
    CHECK(folderFor({"https://a.com/x", "LEIAME", 10, false}) == "(nenhuma)");
    CHECK(dm::defaultRules(false)[1].folder == "Compressed");

    dm::Rule site;
    site.name = "Faculdade";
    site.sites = dm::splitList("moodle.ufrj.br");
    site.nameContains = "AULA";
    site.minSize = 1000;
    site.folder = "D:\\Faculdade";
    site.extract = true;
    rules.insert(rules.begin(), site);
    CHECK(folderFor({"https://www.moodle.ufrj.br/f", "aula3.zip", 5000, false}) == "D:\\Faculdade");
    CHECK(folderFor({"https://moodle.ufrj.br.evil.com/f", "aula3.zip", 5000, false}) == "Compactados");
    CHECK(folderFor({"https://moodle.ufrj.br/f", "aula3.zip", 10, false}) == "Compactados");  // pequeno demais
    CHECK(folderFor({"https://moodle.ufrj.br/f", "prova.zip", 5000, false}) == "Compactados");
    rules[0].enabled = false;
    CHECK(folderFor({"https://moodle.ufrj.br/f", "aula3.zip", 5000, false}) == "Compactados");
    rules[0].enabled = true;

    const auto parsed = dm::parseRules(dm::serializeRules(rules));
    CHECK(parsed.size() == rules.size());
    CHECK(parsed[0].name == "Faculdade" && parsed[0].sites == site.sites && parsed[0].nameContains == "AULA");
    CHECK(parsed[0].minSize == 1000 && parsed[0].extract && !parsed[0].deleteArchive);
    CHECK(parsed[1].kind == dm::Rule::Kind::Video && parsed[2].extensions == rules[2].extensions);
    CHECK(dm::parseRules("lixo").empty());

    CHECK(dm::splitList(" .ZIP, rar;7z  zip") == std::vector<std::string>({"zip", "rar", "7z"}));
    CHECK(dm::joinList({"zip", "rar"}) == "zip, rar");
    CHECK(dm::fileExtension("a/b.c/arquivo") == "" && dm::fileExtension(".bashrc") == "");
    CHECK(dm::resolveRuleFolder("Compactados", "C:\\Downloads") == "C:\\Downloads\\Compactados");
    CHECK(dm::resolveRuleFolder("Compactados", "C:\\Downloads\\") == "C:\\Downloads\\Compactados");
    CHECK(dm::resolveRuleFolder("E:\\Jogos", "C:\\Downloads") == "E:\\Jogos");
    CHECK(dm::resolveRuleFolder("\\\\nas\\x", "C:\\Downloads") == "\\\\nas\\x");
}

}  // namespace

void testUpdate() {
    CHECK(dm::parseVersion("v1.2.3") == (dm::Version{1, 2, 3}));
    CHECK(dm::parseVersion("0.10.0") == (dm::Version{0, 10, 0}));
    CHECK(!dm::parseVersion("1.2") && !dm::parseVersion("1.2.3-beta") && !dm::parseVersion("") &&
          !dm::parseVersion("v1..3") && !dm::parseVersion("1.2.3.4"));
    CHECK(*dm::parseVersion("0.10.0") > *dm::parseVersion("0.9.9"));
    CHECK(dm::formatVersion({2, 0, 11}) == "2.0.11");

    const std::string release = R"({"tag_name":"v0.2.0","draft":false,"prerelease":false,"assets":[
        {"name":"extensao-download-manager.zip","browser_download_url":"https://github.com/a/b/releases/download/v0.2.0/extensao-download-manager.zip"},
        {"name":"DownloadManager-Setup.exe","browser_download_url":"https://github.com/a/b/releases/download/v0.2.0/DownloadManager-Setup.exe"},
        {"name":"DownloadManager-Setup.exe.sha256","browser_download_url":"https://github.com/a/b/releases/download/v0.2.0/DownloadManager-Setup.exe.sha256"}]})";
    const auto info = dm::parseLatestRelease(release);
    CHECK(info && info->version == (dm::Version{0, 2, 0}));
    CHECK(info && info->setupUrl.find("/DownloadManager-Setup.exe") != std::string::npos &&
          info->checksumUrl.ends_with(".sha256"));
    // Sem o .sha256, rascunho, tag estranha ou link fora do GitHub: ignora.
    std::string noChecksum = release;
    noChecksum.replace(noChecksum.find("Setup.exe.sha256\""), 17, "Setup.txt\"");
    CHECK(!dm::parseLatestRelease(noChecksum));
    std::string draft = release;
    draft.replace(draft.find("\"draft\":false"), 13, "\"draft\":true");
    CHECK(!dm::parseLatestRelease(draft));
    std::string badTag = release;
    badTag.replace(badTag.find("v0.2.0"), 6, "latest");
    CHECK(!dm::parseLatestRelease(badTag));
    std::string otherHost = release;
    for (size_t at; (at = otherHost.find("https://github.com/")) != std::string::npos;) {
        otherHost.replace(at, 19, "http://127.0.0.1/");
    }
    CHECK(!dm::parseLatestRelease(otherHost) && dm::parseLatestRelease(otherHost, true));
    CHECK(!dm::parseLatestRelease("not json"));

    const std::string hash(64, 'A');
    CHECK(dm::parseChecksumFile(hash + "  DownloadManager-Setup.exe\n") == std::string(64, 'a'));
    CHECK(dm::parseChecksumFile(hash) == std::string(64, 'a'));
    CHECK(dm::parseChecksumFile("\n" + hash + "\r\n") == std::string(64, 'a'));
    CHECK(dm::parseChecksumFile(hash.substr(1)).empty() && dm::parseChecksumFile(hash + "f").empty() &&
          dm::parseChecksumFile(std::string(63, 'a') + "g").empty());
}

void testDebrid() {
    const std::string magnet =
        "magnet:?xt=urn:btih:0123456789ABCDEF0123456789abcdef01234567&dn=Linux+Mint%2022.iso&tr=udp%3A%2F%2Ft.example";
    CHECK(dm::isMagnetLink(magnet));
    CHECK(dm::magnetHash(magnet) == "0123456789abcdef0123456789abcdef01234567");
    CHECK(dm::magnetDisplayName(magnet) == "Linux Mint 22.iso");
    CHECK(dm::isMagnetLink("MAGNET:?xt=urn:btih:abcdefghijklmnopqrstuvwxyz234567"));  // base32
    CHECK(!dm::isMagnetLink("magnet:?xt=urn:btih:123"));
    CHECK(!dm::isMagnetLink("magnet:?dn=x"));
    CHECK(!dm::isMagnetLink("https://example.com/a.torrent"));
    CHECK(!dm::isMagnetLink(magnet + "\r\nX: y"));

    CHECK(dm::formEncode({{"magnet", "a b&c=d/é"}, {"x", "1"}}) == "magnet=a%20b%26c%3Dd%2F%C3%A9&x=1");

    CHECK(dm::parseDebridError(200, "") == dm::DebridError::None);
    CHECK(dm::parseDebridError(401, R"({"error":"bad_token","error_code":8})") == dm::DebridError::BadToken);
    CHECK(dm::parseDebridError(401, "") == dm::DebridError::BadToken);
    CHECK(dm::parseDebridError(403, R"({"error":"permission_denied","error_code":9})") == dm::DebridError::NotPremium);
    CHECK(dm::parseDebridError(503, R"({"error_code":21})") == dm::DebridError::TooManyTorrents);
    CHECK(dm::parseDebridError(400, R"({"error_code":30})") == dm::DebridError::TorrentFailed);
    CHECK(dm::parseDebridError(500, "x") == dm::DebridError::Other);

    const auto user = dm::parseDebridUser(
        R"({"id":1,"username":"bernardo","email":"x","points":10,"type":"premium","expiration":"2026-12-31T00:00:00.000Z"})");
    CHECK(user && user->username == "bernardo" && user->premium);
    CHECK(dm::formatDebridDate(user->expiration, true) == "31/12/2026");
    CHECK(dm::formatDebridDate(user->expiration, false) == "2026-12-31");
    CHECK(dm::formatDebridDate("", true).empty());
    CHECK(!dm::parseDebridUser("{}"));

    CHECK(dm::parseAddedTorrentId(R"({"id":"ABC123","uri":"https://api.real-debrid.com/x"})") == "ABC123");
    CHECK(dm::parseAddedTorrentId(R"({"id":"../x"})").empty());

    auto torrent = dm::parseDebridTorrent(
        R"({"id":"ABC","filename":"Mint.iso","bytes":2000000000,"progress":45.5,"status":"downloading","speed":1000,"seeders":12,"links":[]})");
    CHECK(torrent && torrent->status == dm::DebridTorrent::Status::Downloading && torrent->progress == 45.5 &&
          torrent->bytes == 2000000000 && torrent->seeders == 12 && torrent->fileName == "Mint.iso");
    torrent = dm::parseDebridTorrent(
        R"({"id":"ABC","filename":"x","status":"downloaded","progress":100,"links":["https:\/\/real-debrid.com\/d\/AAA","ftp://bad"]})");
    CHECK(torrent && torrent->status == dm::DebridTorrent::Status::Ready && torrent->links.size() == 1 &&
          torrent->links[0] == "https://real-debrid.com/d/AAA");
    torrent = dm::parseDebridTorrent(R"({"id":"ABC","status":"downloaded","links":[]})");
    CHECK(torrent && torrent->status == dm::DebridTorrent::Status::Failed);
    torrent = dm::parseDebridTorrent(R"({"id":"ABC","status":"waiting_files_selection"})");
    CHECK(torrent && torrent->status == dm::DebridTorrent::Status::WaitingSelection);
    torrent = dm::parseDebridTorrent(R"({"id":"ABC","status":"dead"})");
    CHECK(torrent && torrent->status == dm::DebridTorrent::Status::Failed);

    const auto link = dm::parseDebridLink(
        R"({"id":"X","filename":"Mint.iso","filesize":2000,"link":"https://real-debrid.com/d/AAA","download":"https://dl.real-debrid.com/x/Mint.iso"})");
    CHECK(link && link->download == "https://dl.real-debrid.com/x/Mint.iso" && link->fileName == "Mint.iso" &&
          link->size == 2000);
    CHECK(!dm::parseDebridLink(R"({"download":"javascript:x"})"));
}

int main() {
    testContentRange();
    testFileNames();
    testFormat();
    testResumeState();
    testSplitting();
    testReleaseAndRestore();
    testConcurrentCoverage();
    testDownloadList();
    testSettings();
    testRateLimiter();
    testSchedule();
    testJson();
    testBrowserRequest();
    testBase64();
    testVideo();
    testCommandLine();
    testRules();
    testUpdate();
    testDebrid();

    if (g_failures == 0) std::printf("Todos os testes passaram.\n");
    return g_failures == 0 ? 0 : 1;
}
