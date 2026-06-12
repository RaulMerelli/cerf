#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>
#include <type_traits>

class StateWriter {
public:
    /* Writes to "<path>.tmp"; Commit() swaps it in. Opening `path`
       directly would let an aborted save truncate the last good image.
       (DeviceEmulator state.cpp Save() uses the same temp-then-swap.) */
    explicit StateWriter(const std::wstring& path);
    ~StateWriter();

    StateWriter(const StateWriter&)            = delete;
    StateWriter& operator=(const StateWriter&) = delete;

    bool Ok() const { return ok_; }

    void WriteBytes(const void* src, size_t n);

    template <typename T>
    void Write(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateWriter::Write requires a trivially copyable POD; "
                      "serialize non-POD field-by-field.");
        WriteBytes(&v, sizeof(T));
    }

    uint64_t BytesWritten() const { return bytes_written_; }

    /* Overwrite n bytes at absolute `offset` (already written), then seek
       back to the end. Back-patches a section-length placeholder once the
       body length is known. */
    void PatchAt(uint64_t offset, const void* src, size_t n);

    bool Commit();

private:
    void CloseHandle_();

    std::wstring final_path_;
    std::wstring temp_path_;
    HANDLE       file_          = INVALID_HANDLE_VALUE;
    uint64_t     bytes_written_ = 0;
    bool         ok_            = false;
    bool         committed_     = false;
};

class StateReader {
public:
    /* Opens path for reading (OPEN_EXISTING, shared read). Ok() is false
       if the file is absent or unopenable. */
    explicit StateReader(const std::wstring& path);
    ~StateReader();

    StateReader(const StateReader&)            = delete;
    StateReader& operator=(const StateReader&) = delete;

    bool Ok() const { return ok_; }

    /* Reads n bytes into dst. A short read marks the stream failed. */
    void ReadBytes(void* dst, size_t n);

    template <typename T>
    void Read(T& v) {
        static_assert(std::is_trivially_copyable_v<T>,
                      "StateReader::Read requires a trivially copyable POD; "
                      "deserialize non-POD field-by-field.");
        ReadBytes(&v, sizeof(T));
    }

    uint64_t BytesRead() const { return bytes_read_; }
    uint64_t FileSize()  const { return file_size_; }

    uint64_t Position() const;        /* current absolute file offset */
    void     SeekTo(uint64_t offset); /* re-align to a section boundary */

private:
    HANDLE   file_      = INVALID_HANDLE_VALUE;
    uint64_t bytes_read_ = 0;
    uint64_t file_size_  = 0;
    bool     ok_         = false;
};
