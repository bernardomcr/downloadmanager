#include "core/resume_state.h"

#include <sstream>

namespace dm {
namespace {

constexpr const char* kHeader = "dmstate 1";

std::string singleLine(const std::string& value) {
    std::string result;
    for (const char c : value) result += (c == '\r' || c == '\n') ? ' ' : c;
    return result;
}

}  // namespace

bool ResumeState::matches(int64_t size, const std::string& otherEtag, const std::string& otherLastModified) const {
    if (size != totalSize) return false;
    if (!etag.empty() && !otherEtag.empty()) return etag == otherEtag;
    if (!lastModified.empty() && !otherLastModified.empty()) return lastModified == otherLastModified;
    return true;  // sem validadores: confiamos no tamanho
}

std::string serializeResumeState(const ResumeState& state) {
    std::ostringstream out;
    out << kHeader << '\n';
    out << "url " << singleLine(state.url) << '\n';
    out << "name " << singleLine(state.fileName) << '\n';
    out << "size " << state.totalSize << '\n';
    out << "etag " << singleLine(state.etag) << '\n';
    out << "last-modified " << singleLine(state.lastModified) << '\n';
    for (const auto& segment : state.segments) {
        out << "segment " << segment.start << ' ' << segment.written << ' ' << segment.end << '\n';
    }
    return out.str();
}

std::optional<ResumeState> parseResumeState(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != kHeader) return std::nullopt;

    ResumeState state;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t space = line.find(' ');
        const std::string key = line.substr(0, space);
        const std::string value = space == std::string::npos ? "" : line.substr(space + 1);

        if (key == "url") state.url = value;
        else if (key == "name") state.fileName = value;
        else if (key == "etag") state.etag = value;
        else if (key == "last-modified") state.lastModified = value;
        else if (key == "size") {
            std::istringstream number(value);
            if (!(number >> state.totalSize)) return std::nullopt;
        } else if (key == "segment") {
            std::istringstream numbers(value);
            Segment segment;
            if (!(numbers >> segment.start >> segment.written >> segment.end)) return std::nullopt;
            if (segment.start > segment.written || segment.written > segment.end) return std::nullopt;
            segment.reserved = segment.written;
            state.segments.push_back(segment);
        }
    }

    if (state.url.empty() || state.totalSize <= 0 || state.segments.empty()) return std::nullopt;
    for (const auto& segment : state.segments) {
        if (segment.end > state.totalSize) return std::nullopt;
    }
    return state;
}

}  // namespace dm
