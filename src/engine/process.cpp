#include "engine/process.h"

#include "util/unicode.h"

namespace dm {

Process::~Process() {
    kill();
    close();
}

bool Process::start(const std::string& commandLine, const std::wstring& workingDirectory) {
    close();  // a mesma instância pode rodar de novo (continuar um vídeo pausado)
    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) return false;
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    // Só a ponta de escrita é herdada; stdin fica vazio (NUL) para o programa nunca esperar entrada.
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0,
                             nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    std::wstring command = toWide(commandLine);
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr,
                                        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &info);
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!created) {
        CloseHandle(readEnd);
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, info.hProcess);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);

    std::lock_guard lock(mutex_);
    process_ = info.hProcess;
    job_ = job;
    output_ = readEnd;
    return true;
}

int Process::run(const std::function<void(const std::string&)>& onLine) {
    HANDLE output;
    {
        std::lock_guard lock(mutex_);
        output = output_;
    }
    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (output && ReadFile(output, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        pending.append(buffer, read);
        size_t newline;
        // O yt-dlp usa \r para atualizar a mesma linha e \n para linhas novas: os dois separam.
        while ((newline = pending.find_first_of("\r\n")) != std::string::npos) {
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && onLine) onLine(line);
        }
    }
    if (!pending.empty() && onLine) onLine(pending);

    HANDLE process;
    {
        std::lock_guard lock(mutex_);
        process = process_;
    }
    DWORD exitCode = static_cast<DWORD>(-1);
    if (process) {
        // Sem o lock: kill() precisa conseguir agir enquanto esperamos.
        WaitForSingleObject(process, INFINITE);
        GetExitCodeProcess(process, &exitCode);
    }
    return static_cast<int>(exitCode);
}

void Process::kill() {
    std::lock_guard lock(mutex_);
    if (job_) {
        TerminateJobObject(job_, 1);
    } else if (process_) {
        TerminateProcess(process_, 1);
    }
}

void Process::close() {
    std::lock_guard lock(mutex_);
    for (HANDLE* handle : {&process_, &job_, &output_}) {
        if (*handle) CloseHandle(*handle);
        *handle = nullptr;
    }
}

int runAndCapture(const std::string& commandLine, std::string& output) {
    Process process;
    if (!process.start(commandLine)) return -1;
    output.clear();
    return process.run([&](const std::string& line) { output += line + "\n"; });
}

}  // namespace dm
