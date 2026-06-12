#include "state_stream.h"

#include "../core/log.h"

namespace {
/* WriteFile / ReadFile take a DWORD length; a single RAM region can
   exceed that. Transfer in bounded chunks. */
constexpr DWORD kIoChunk = 0x40000000u;  /* 1 GiB */
}

StateWriter::StateWriter(const std::wstring& path)
    : final_path_(path), temp_path_(path + L".tmp") {
    file_ = CreateFileW(temp_path_.c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        LOG(Caution, "StateWriter: CreateFile('%ls') failed gle=%lu\n",
            temp_path_.c_str(), GetLastError());
        return;
    }
    ok_ = true;
}

StateWriter::~StateWriter() {
    CloseHandle_();
    if (!committed_)
        DeleteFileW(temp_path_.c_str());
}

void StateWriter::CloseHandle_() {
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

void StateWriter::WriteBytes(const void* src, size_t n) {
    if (!ok_) return;
    const auto* p = static_cast<const uint8_t*>(src);
    while (n > 0) {
        const DWORD chunk = n > kIoChunk ? kIoChunk : static_cast<DWORD>(n);
        DWORD wrote = 0;
        if (!WriteFile(file_, p, chunk, &wrote, nullptr) || wrote != chunk) {
            LOG(Caution, "StateWriter: WriteFile failed gle=%lu (%lu/%lu)\n",
                GetLastError(), wrote, chunk);
            ok_ = false;
            return;
        }
        p              += chunk;
        n              -= chunk;
        bytes_written_ += chunk;
    }
}

void StateWriter::PatchAt(uint64_t offset, const void* src, size_t n) {
    if (!ok_) return;
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file_, li, nullptr, FILE_BEGIN)) {
        LOG(Caution, "StateWriter: PatchAt seek failed gle=%lu\n", GetLastError());
        ok_ = false;
        return;
    }
    DWORD wrote = 0;
    if (!WriteFile(file_, src, static_cast<DWORD>(n), &wrote, nullptr) || wrote != n) {
        LOG(Caution, "StateWriter: PatchAt write failed gle=%lu\n", GetLastError());
        ok_ = false;
        return;
    }
    LARGE_INTEGER end{};
    if (!SetFilePointerEx(file_, end, nullptr, FILE_END)) {
        LOG(Caution, "StateWriter: PatchAt seek-back failed gle=%lu\n", GetLastError());
        ok_ = false;
    }
}

bool StateWriter::Commit() {
    if (!ok_) {
        CloseHandle_();
        DeleteFileW(temp_path_.c_str());
        return false;
    }
    CloseHandle_();
    if (!MoveFileExW(temp_path_.c_str(), final_path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOG(Caution, "StateWriter: MoveFileEx('%ls' -> '%ls') failed gle=%lu\n",
            temp_path_.c_str(), final_path_.c_str(), GetLastError());
        DeleteFileW(temp_path_.c_str());
        ok_ = false;
        return false;
    }
    committed_ = true;
    return true;
}

StateReader::StateReader(const std::wstring& path) {
    file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(file_, &sz))
        file_size_ = static_cast<uint64_t>(sz.QuadPart);
    ok_ = true;
}

StateReader::~StateReader() {
    if (file_ != INVALID_HANDLE_VALUE)
        CloseHandle(file_);
}

void StateReader::ReadBytes(void* dst, size_t n) {
    if (!ok_) return;
    auto* p = static_cast<uint8_t*>(dst);
    while (n > 0) {
        const DWORD chunk = n > kIoChunk ? kIoChunk : static_cast<DWORD>(n);
        DWORD got = 0;
        if (!ReadFile(file_, p, chunk, &got, nullptr) || got != chunk) {
            LOG(Caution, "StateReader: ReadFile failed gle=%lu (%lu/%lu)\n",
                GetLastError(), got, chunk);
            ok_ = false;
            return;
        }
        p          += chunk;
        n          -= chunk;
        bytes_read_ += chunk;
    }
}

uint64_t StateReader::Position() const {
    if (!ok_) return 0;
    LARGE_INTEGER zero{}, cur{};
    if (!SetFilePointerEx(file_, zero, &cur, FILE_CURRENT)) return 0;
    return static_cast<uint64_t>(cur.QuadPart);
}

void StateReader::SeekTo(uint64_t offset) {
    if (!ok_) return;
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file_, li, nullptr, FILE_BEGIN)) {
        LOG(Caution, "StateReader: SeekTo %llu failed gle=%lu\n",
            static_cast<unsigned long long>(offset), GetLastError());
        ok_ = false;
        return;
    }
    bytes_read_ = offset;
}
