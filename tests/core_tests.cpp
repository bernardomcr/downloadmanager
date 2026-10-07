// Testes da parte portátil (roda no Windows e no Linux). Sem framework: cada CHECK que falha
// imprime o local e o processo termina com código 1.
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include "core/download_list.h"
#include "core/format.h"
#include "core/http_headers.h"
#include "core/rate_limiter.h"
#include "core/resume_state.h"
#include "core/segments.h"
#include "core/settings.h"

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
    record.errorCode = 3;
    record.errorDetail = 404;

    dm::DownloadRecord second = record;
    second.id = 8;
    second.state = dm::RecordState::Queued;

    const auto parsed = dm::parseDownloadList(dm::serializeDownloadList({record, second}));
    CHECK(parsed.size() == 2);
    CHECK(parsed[0].id == 7 && parsed[0].directory == record.directory && parsed[0].filePath == record.filePath);
    CHECK(parsed[0].state == dm::RecordState::Completed && parsed[0].totalSize == 1234);
    CHECK(parsed[0].finishedAt == 1700000100 && parsed[0].connections == 4);
    CHECK(parsed[0].errorCode == 3 && parsed[0].errorDetail == 404);
    CHECK(parsed[1].state == dm::RecordState::Queued && parsed[0].speedLimit == 51200);

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
    const auto parsed = dm::parseSettings(dm::serializeSettings(settings));
    CHECK(parsed.downloadFolder == "D:\\Baixados" && parsed.connections == 16);
    CHECK(parsed.language == dm::LanguageSetting::English && !parsed.closeToTray);
    CHECK(parsed.startWithWindows && parsed.notifyOnComplete && parsed.keepAwake);
    CHECK(parsed.maxDownloads == 5 && parsed.speedLimitKBps == 300 && parsed.scheduleEnabled);
    CHECK(parsed.scheduleStart == 23 * 60 && parsed.scheduleEnd == 6 * 60 + 30);
    CHECK(parsed.whenDone == dm::WhenDone::Nothing);  // não é salvo
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

}  // namespace

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

    if (g_failures == 0) std::printf("Todos os testes passaram.\n");
    return g_failures == 0 ? 0 : 1;
}
