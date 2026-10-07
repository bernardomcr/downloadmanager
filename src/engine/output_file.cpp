#include "engine/output_file.h"

#include <winioctl.h>

namespace dm {

OutputFile::~OutputFile() {
    close();
}

bool OutputFile::open(const std::wstring& path, int64_t size, bool keepExisting, DWORD& errorCode) {
    close();
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          keepExisting ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        errorCode = GetLastError();
        return false;
    }

    if (size >= 0) {
        DWORD ignored = 0;
        sparse_ = DeviceIoControl(handle_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ignored, nullptr) != 0;

        FILE_END_OF_FILE_INFO endOfFile{};
        endOfFile.EndOfFile.QuadPart = size;
        if (!SetFileInformationByHandle(handle_, FileEndOfFileInfo, &endOfFile, sizeof(endOfFile))) {
            errorCode = GetLastError();
            close();
            return false;
        }
    }
    return true;
}

bool OutputFile::writeAt(int64_t offset, const void* data, size_t length, DWORD& errorCode) {
    const auto* bytes = static_cast<const char*>(data);
    while (length > 0) {
        OVERLAPPED position{};
        position.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        position.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD written = 0;
        if (!WriteFile(handle_, bytes, static_cast<DWORD>(length), &written, &position) || written == 0) {
            errorCode = GetLastError();
            return false;
        }
        bytes += written;
        offset += written;
        length -= written;
    }
    return true;
}

void OutputFile::flush() {
    if (handle_ != INVALID_HANDLE_VALUE) FlushFileBuffers(handle_);
}

void OutputFile::finish() {
    if (handle_ != INVALID_HANDLE_VALUE && sparse_) {
        FILE_SET_SPARSE_BUFFER notSparse{FALSE};
        DWORD ignored = 0;
        DeviceIoControl(handle_, FSCTL_SET_SPARSE, &notSparse, sizeof(notSparse), nullptr, 0, &ignored, nullptr);
    }
    close();
}

void OutputFile::close() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    sparse_ = false;
}

}  // namespace dm
