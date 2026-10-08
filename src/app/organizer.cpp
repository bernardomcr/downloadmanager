#include "app/organizer.h"

#include <windows.h>
#include <shlobj.h>

#include "app/extractor.h"
#include "core/archive.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {

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

    if (job.rule.extract && dm::isArchiveExtension(dm::fileExtension(dm::toUtf8(target)))) {
        const ExtractResult extracted = smartExtract(target);
        if (extracted.ok && job.rule.deleteArchive) {
            DeleteFileW(target.c_str());
            result.path = extracted.path;
        }
    }
    return result;
}

}  // namespace app
