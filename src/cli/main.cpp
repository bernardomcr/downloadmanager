// dm-cli: usa o mesmo motor do app pela linha de comando. Serve para testar o motor e para scripts.
//   dm-cli <link> [pasta] [--conexoes N] [--nome arquivo] [--limite KB/s]
//   dm-cli --video <link> [pasta] [--qualidade best|1080|720|mp3|audio]   (vídeo pelo yt-dlp)
//   dm-cli --preparar-videos <pasta-de-dados>   (baixa yt-dlp e ffmpeg; usado no CI)
//   dm-cli --organizar <arquivo> <pasta-base> [--extrair]   (aplica as regras padrão; usado no CI)
// Ctrl+C pausa e salva o progresso; rodar o mesmo comando de novo continua de onde parou.
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <string>

#include "app/extractor.h"
#include "app/organizer.h"
#include "app/video_tools.h"
#include "core/format.h"
#include "core/rules.h"
#include "core/video.h"
#include "engine/download_task.h"
#include "engine/video_task.h"
#include "i18n/errors.h"
#include "i18n/strings.h"
#include "util/file_io.h"
#include "util/unicode.h"

using i18n::Str;
using i18n::tr;

namespace {

std::atomic<dm::Task*> g_task{nullptr};

BOOL WINAPI onConsoleSignal(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (dm::Task* task = g_task) task->pause();
        return TRUE;
    }
    return FALSE;
}

void print(const std::wstring& text) {
    const std::string utf8 = dm::toUtf8(text);
    std::fwrite(utf8.data(), 1, utf8.size(), stdout);
    std::fflush(stdout);
}

std::wstring progressLine(const dm::DownloadProgress& progress, char decimal) {
    std::string line = dm::formatBytes(progress.downloaded, decimal);
    if (progress.totalSize > 0) {
        char percent[16];
        std::snprintf(percent, sizeof(percent), "%5.1f%%",
                      100.0 * static_cast<double>(progress.downloaded) / static_cast<double>(progress.totalSize));
        line = std::string(percent) + "  " + line + " / " + dm::formatBytes(progress.totalSize, decimal);
    }
    line += "  " + dm::formatSpeed(progress.bytesPerSecond, decimal);
    if (progress.totalSize > 0 && progress.bytesPerSecond > 1) {
        const auto seconds = static_cast<int64_t>(static_cast<double>(progress.totalSize - progress.downloaded) /
                                                  progress.bytesPerSecond);
        line += "  " + dm::formatDuration(seconds);
    }
    line += "  [" + std::to_string(progress.activeConnections) + "]";
    return L"\r" + dm::toWide(line) + L"          ";
}

}  // namespace

// Baixa e instala o yt-dlp e o ffmpeg em <dados>\tools, mostrando o progresso.
int prepareVideoTools(const std::wstring& dataDirectory) {
    CreateDirectoryW(dataDirectory.c_str(), nullptr);
    app::VideoTools tools(dataDirectory);
    tools.install();
    while (tools.state() == app::VideoTools::State::Installing) {
        Sleep(500);
        print(L"\r" + std::to_wstring(static_cast<int>(tools.installProgress() * 100)) + L"%   ");
    }
    print(L"\n");
    if (tools.state() != app::VideoTools::State::Ready) {
        print(std::wstring(tr(Str::VideoToolsFailed)) + L"\n");
        return 1;
    }
    print(tools.ytDlpPath() + L"\n");
    return 0;
}

int downloadVideo(const std::string& url, const std::wstring& directory, const std::string& quality) {
    wchar_t localAppData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    app::VideoTools tools(std::wstring(localAppData, length) + L"\\DownloadManager");
    if (!tools.ready()) {
        print(std::wstring(tr(Str::VideoToolsFailed)) + L"\n");
        return 1;
    }
    dm::VideoTaskOptions options;
    options.ytDlpPath = tools.ytDlpPath();
    options.tempDirectory = tools.tempDirectory();
    options.job.url = url;
    options.job.outputDirectory = dm::toUtf8(directory);
    options.job.format = dm::VideoFormat::parse(quality);
    options.job.ffmpegDirectory = dm::toUtf8(tools.ffmpegDirectory());
    dm::VideoTask task(options);
    g_task = &task;
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);
    task.start();
    dm::DownloadProgress progress = task.progress();
    while (progress.status == dm::DownloadStatus::Connecting || progress.status == dm::DownloadStatus::Downloading) {
        Sleep(500);
        progress = task.progress();
        if (progress.status == dm::DownloadStatus::Downloading) {
            print(progressLine(progress, i18n::decimalSeparator()) + (progress.postProcessing ? L" ..." : L""));
        }
    }
    task.wait();
    progress = task.progress();
    g_task = nullptr;
    print(L"\n");
    if (progress.status == dm::DownloadStatus::Paused) {
        print(std::wstring(tr(Str::CliPaused)) + L"\n");
        return 2;
    }
    if (progress.status != dm::DownloadStatus::Completed) {
        print(i18n::describeError(progress.error, progress.errorDetail, progress.errorText) + L"\n");
        return 1;
    }
    wchar_t message[1024];
    std::swprintf(message, 1024, tr(Str::CliCompleted), progress.filePath.c_str());
    print(std::wstring(message) + L"\n");
    return 0;
}

// Aplica as regras padrão a um arquivo, como o app faz ao concluir. --extrair liga "extrair e apagar o compactado".
int organize(const std::wstring& file, const std::wstring& baseFolder, bool extract) {
    const auto rules = dm::defaultRules(i18n::currentLanguage() == i18n::Language::Portuguese);
    const dm::DownloadFacts facts{{}, dm::toUtf8(dm::fileNameOf(file)), -1, false};
    const dm::Rule* match = dm::matchRule(rules, facts);
    if (!match) {
        print(file + L"\n");
        return 0;
    }
    dm::Rule rule = *match;
    rule.extract = rule.deleteArchive = extract;
    app::Organizer organizer;
    organizer.submit({1, file, baseFolder, rule});
    organizer.drain();
    const auto results = organizer.takeResults();
    if (results.empty()) return 1;
    print(results.front().path + L"\n");
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    i18n::setLanguage(i18n::systemLanguage());
    const char decimal = i18n::decimalSeparator();

    if (argc >= 3 && std::wstring(argv[1]) == L"--preparar-videos") return prepareVideoTools(argv[2]);
    if (argc >= 3 && std::wstring(argv[1]) == L"--extrator") {
        // Diagnóstico: qual programa extrairia um arquivo com esta extensão.
        const app::Extractor extractor = app::findExtractor(argv[2]);
        std::printf("%s\n", dm::toUtf8(extractor.name + L" " + extractor.exe).c_str());
        return extractor.kind == app::Extractor::Kind::None ? 1 : 0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"--organizar") {
        return organize(argv[2], argv[3], argc >= 5 && std::wstring(argv[4]) == L"--extrair");
    }
    if (argc >= 3 && std::wstring(argv[1]) == L"--video") {
        std::string quality = "best";
        std::wstring directory = L".";
        for (int i = 3; i < argc; ++i) {
            if (std::wstring(argv[i]) == L"--qualidade" && i + 1 < argc) {
                quality = dm::toUtf8(argv[++i]);
            } else {
                directory = argv[i];
            }
        }
        return downloadVideo(dm::toUtf8(argv[2]), directory, quality);
    }

    dm::DownloadOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if ((argument == L"--conexoes" || argument == L"--connections") && i + 1 < argc) {
            options.connections = _wtoi(argv[++i]);
        } else if ((argument == L"--limite" || argument == L"--limit") && i + 1 < argc) {
            options.speedLimit = static_cast<int64_t>(_wtoi(argv[++i])) * 1024;
        } else if ((argument == L"--nome" || argument == L"--name") && i + 1 < argc) {
            options.fileName = argv[++i];
        } else if ((argument == L"--tamanho" || argument == L"--size") && i + 1 < argc) {
            options.knownSize = _wtoi64(argv[++i]);  // com --nome: início rápido (como os links do Real-Debrid)
        } else if (options.url.empty()) {
            options.url = dm::toUtf8(argument);
        } else if (options.directory.empty()) {
            options.directory = argument;
        }
    }
    if (options.url.empty()) {
        print(std::wstring(tr(Str::CliUsage)) + L"\n");
        return 64;
    }
    if (options.directory.empty()) options.directory = L".";

    dm::DownloadTask task(options);
    g_task = &task;
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);
    task.start();

    // Confere a cada 50 ms (o fim do download aparece na hora, e medir o tempo com o dm-cli é justo);
    // a linha de progresso continua a cada 0,5 s.
    dm::DownloadProgress progress = task.progress();
    for (int tick = 1; progress.status == dm::DownloadStatus::Connecting ||
                       progress.status == dm::DownloadStatus::Downloading;
         ++tick) {
        Sleep(50);
        progress = task.progress();
        if (tick % 10 == 0 && progress.status == dm::DownloadStatus::Downloading) print(progressLine(progress, decimal));
    }
    task.wait();
    progress = task.progress();
    g_task = nullptr;
    print(L"\n");

    switch (progress.status) {
        case dm::DownloadStatus::Completed: {
            wchar_t message[1024];
            std::swprintf(message, 1024, tr(Str::CliCompleted), progress.filePath.c_str());
            print(std::wstring(message) + L"\n");
            return 0;
        }
        case dm::DownloadStatus::Paused:
            print(std::wstring(tr(progress.resumable ? Str::CliPaused : Str::CliPausedNotResumable)) + L"\n");
            return 2;
        default:
            print(i18n::describeError(progress.error, progress.errorDetail) + L"\n");
            return 1;
    }
}
