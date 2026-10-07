#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/segments.h"

namespace dm {

// Tudo que é preciso para continuar um download depois de pausa, travamento ou reinício.
// Fica num arquivo .dmstate ao lado do .dmpart. Strings em UTF-8.
struct ResumeState {
    std::string url;
    std::string fileName;
    int64_t totalSize = -1;
    std::string etag;
    std::string lastModified;
    std::vector<Segment> segments;

    // O arquivo no servidor ainda é o mesmo que começamos a baixar?
    bool matches(int64_t size, const std::string& otherEtag, const std::string& otherLastModified) const;
};

std::string serializeResumeState(const ResumeState& state);
std::optional<ResumeState> parseResumeState(const std::string& text);

}  // namespace dm
