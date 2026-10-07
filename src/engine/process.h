#pragma once

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>

namespace dm {

// Programa externo (yt-dlp, ffmpeg, tar) rodando escondido, com a saída lida linha a linha.
// Fica num Job Object: matar o processo leva junto os filhos (ex.: ffmpeg aberto pelo yt-dlp).
class Process {
public:
    Process() = default;
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // commandLine em UTF-8 (use buildCommandLine). Saída e erros chegam juntos em onLine, sem o \n.
    bool start(const std::string& commandLine, const std::wstring& workingDirectory = {});
    // Lê a saída até o processo terminar. Devolve o código de saída (ou -1).
    int run(const std::function<void(const std::string&)>& onLine);
    // Mata o processo e os filhos. Pode ser chamado de outra thread.
    void kill();

private:
    void close();

    std::mutex mutex_;
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    HANDLE output_ = nullptr;
};

// Roda até o fim e devolve toda a saída (para comandos curtos, como a análise do yt-dlp).
int runAndCapture(const std::string& commandLine, std::string& output);

}  // namespace dm
