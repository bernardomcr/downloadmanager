#pragma once

#include <optional>
#include <string>

namespace dm {

bool fileExists(const std::wstring& path);
std::optional<std::string> readTextFile(const std::wstring& path);
// Grava num .tmp e troca de uma vez: um travamento no meio nunca deixa o arquivo corrompido.
bool writeTextFileAtomically(const std::wstring& path, const std::string& text);

// "C:\\a\\b.zip" -> "b.zip" / "C:\\a"
std::wstring fileNameOf(const std::wstring& path);
std::wstring directoryOf(const std::wstring& path);
std::wstring joinPath(const std::wstring& directory, const std::wstring& name);

}  // namespace dm
