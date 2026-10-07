#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace dm {

// Arquivo de destino gravado em posições arbitrárias por várias conexões ao mesmo tempo.
class OutputFile {
public:
    OutputFile() = default;
    ~OutputFile();
    OutputFile(const OutputFile&) = delete;
    OutputFile& operator=(const OutputFile&) = delete;

    // size >= 0 reserva o tamanho final (arquivo esparso: sem custo de preencher com zeros).
    // keepExisting preserva o conteúdo já baixado (retomada).
    bool open(const std::wstring& path, int64_t size, bool keepExisting, DWORD& errorCode);
    // Seguro para chamar de várias threads.
    bool writeAt(int64_t offset, const void* data, size_t length, DWORD& errorCode);
    void flush();
    // Download completo: tira a marcação de esparso e fecha.
    void finish();
    void close();

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool sparse_ = false;
};

}  // namespace dm
