#include "app/organizer.h"

#include <windows.h>
#include <shlobj.h>

#include "core/command_line.h"
#include "engine/process.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

bool isArchive(const std::string& extension) {
    for (const char* known : {"zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "zst"}) {
        if (extension == known) return true;
    }
    return false;
}

std::wstring withoutExtension(const std::wstring& name) {
    std::wstring stem = name;
    // "fotos.tar.gz" -> "fotos"
    for (int i = 0; i < 2; ++i) {
        const size_t dot = stem.find_last_of(L'.');
        if (dot == std::wstring::npos || dot == 0) break;
        const std::wstring extension = stem.substr(dot + 1);
        if (i == 1 && extension != L"tar") break;
        stem = stem.substr(0, dot);
    }
    return stem;
}

// tar.exe do Windows 10/11 abre zip, tar.* e, no Windows 11 atual, rar e 7z.
bool extractArchive(const std::wstring& archive, const std::wstring& destination) {
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring tar = dm::joinPath(std::wstring(system, length), L"tar.exe");
    if (!dm::fileExists(tar)) return false;
    if (SHCreateDirectoryExW(nullptr, destination.c_str(), nullptr) != ERROR_SUCCESS &&
        GetLastError() != ERROR_ALREADY_EXISTS && !dm::fileExists(destination)) {
        return false;
    }
    std::string output;
    const int code = dm::runAndCapture(
        dm::buildCommandLine(dm::toUtf8(tar), {"-xf", dm::toUtf8(archive), "-C", dm::toUtf8(destination)}), output);
    if (code != 0) RemoveDirectoryW(destination.c_str());  // só some se ficou vazia; o compactado é mantido
    return code == 0;
}

}  // namespace

Organizer::Organizer() : worker_(&Organizer::run, this) {}

Organizer::~Organizer() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

void Organizer::submit(Job job) {
    {
        std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

std::vector<Organizer::Result> Organizer::takeResults() {
    std::lock_guard lock(mutex_);
    std::vector<Result> results;
    results.swap(results_);
    return results;
}

void Organizer::drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&] { return jobs_.empty() && !working_; });
}

void Organizer::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) return;  // parando e sem trabalho pendente
            job = std::move(jobs_.front());
            jobs_.pop_front();
            working_ = true;
        }
        Result result = organize(job);
        {
            std::lock_guard lock(mutex_);
            results_.push_back(std::move(result));
            working_ = false;
        }
        idle_.notify_all();
    }
}

Organizer::Result Organizer::organize(const Job& job) {
    Result result{job.id, job.path, job.rule.openFile, job.rule.openFolder};
    const std::wstring folder =
        dm::toWide(dm::resolveRuleFolder(job.rule.folder, dm::toUtf8(job.baseFolder)));
    if (folder.empty() || !dm::fileExists(job.path)) return result;

    std::wstring target = job.path;
    if (_wcsicmp(dm::directoryOf(job.path).c_str(), folder.c_str()) != 0) {
        SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        target = dm::uniquePath(folder, dm::fileNameOf(job.path));
        if (!MoveFileExW(job.path.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
            return result;  // não deu para mover (arquivo em uso?): fica onde está
        }
        result.path = target;
    }

    if (job.rule.extract && isArchive(dm::fileExtension(dm::toUtf8(target)))) {
        const std::wstring destination = dm::uniquePath(folder, withoutExtension(dm::fileNameOf(target)));
        if (extractArchive(target, destination)) {
            if (job.rule.deleteArchive) {
                DeleteFileW(target.c_str());
                result.path = destination;
            }
        }
    }
    return result;
}

}  // namespace app
