#include "core/download_list.h"

#include <sstream>

namespace dm {
namespace {

constexpr const char* kHeader = "dmlist 1";
constexpr const char* kItem = "[download]";

const char* stateName(RecordState state) {
    switch (state) {
        case RecordState::Active: return "active";
        case RecordState::Queued: return "queued";
        case RecordState::Paused: return "paused";
        case RecordState::Failed: return "failed";
        case RecordState::Completed: return "completed";
    }
    return "paused";
}

RecordState parseState(const std::string& text) {
    if (text == "active") return RecordState::Active;
    if (text == "queued") return RecordState::Queued;
    if (text == "failed") return RecordState::Failed;
    if (text == "completed") return RecordState::Completed;
    return RecordState::Paused;
}

std::string singleLine(const std::string& value) {
    std::string result;
    for (const char c : value) result += (c == '\r' || c == '\n') ? ' ' : c;
    return result;
}

template <typename Number>
void parseNumber(const std::string& text, Number& target) {
    std::istringstream in(text);
    Number value{};
    if (in >> value) target = value;
}

}  // namespace

std::string serializeDownloadList(const std::vector<DownloadRecord>& records) {
    std::ostringstream out;
    out << kHeader << '\n';
    for (const auto& record : records) {
        out << '\n' << kItem << '\n';
        out << "id " << record.id << '\n';
        out << "url " << singleLine(record.url) << '\n';
        out << "directory " << singleLine(record.directory) << '\n';
        out << "name " << singleLine(record.fileName) << '\n';
        out << "path " << singleLine(record.filePath) << '\n';
        out << "state " << stateName(record.state) << '\n';
        out << "total " << record.totalSize << '\n';
        out << "downloaded " << record.downloaded << '\n';
        out << "added " << record.addedAt << '\n';
        out << "finished " << record.finishedAt << '\n';
        out << "connections " << record.connections << '\n';
        out << "limit " << record.speedLimit << '\n';
        if (!record.protectedHeaders.empty()) out << "headers " << singleLine(record.protectedHeaders) << '\n';
        if (!record.userAgent.empty()) out << "agent " << singleLine(record.userAgent) << '\n';
        out << "error " << record.errorCode << ' ' << record.errorDetail << '\n';
    }
    return out.str();
}

std::vector<DownloadRecord> parseDownloadList(const std::string& text) {
    std::vector<DownloadRecord> records;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return records;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kHeader) return records;

    DownloadRecord current;
    bool inItem = false;
    auto flush = [&] {
        if (inItem && current.id != 0 && !current.url.empty()) records.push_back(current);
        current = DownloadRecord{};
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == kItem) {
            flush();
            inItem = true;
            continue;
        }
        if (!inItem || line.empty()) continue;

        const size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string value = space == std::string::npos ? "" : line.substr(space + 1);

        if (key == "id") parseNumber(value, current.id);
        else if (key == "url") current.url = value;
        else if (key == "directory") current.directory = value;
        else if (key == "name") current.fileName = value;
        else if (key == "path") current.filePath = value;
        else if (key == "state") current.state = parseState(value);
        else if (key == "total") parseNumber(value, current.totalSize);
        else if (key == "downloaded") parseNumber(value, current.downloaded);
        else if (key == "added") parseNumber(value, current.addedAt);
        else if (key == "finished") parseNumber(value, current.finishedAt);
        else if (key == "connections") parseNumber(value, current.connections);
        else if (key == "limit") parseNumber(value, current.speedLimit);
        else if (key == "headers") current.protectedHeaders = value;
        else if (key == "agent") current.userAgent = value;
        else if (key == "error") {
            std::istringstream numbers(value);
            numbers >> current.errorCode >> current.errorDetail;
        }
    }
    flush();
    return records;
}

}  // namespace dm
