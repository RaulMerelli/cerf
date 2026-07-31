#pragma once

#include <cstddef>
#include <cstdint>

class JitCodeArena {
public:
    JitCodeArena() = default;
    ~JitCodeArena();

    JitCodeArena(const JitCodeArena&)            = delete;
    JitCodeArena& operator=(const JitCodeArena&) = delete;

    void Initialize();

    /* Bump-allocate `size` bytes of executable memory. Returns
       nullptr when the remaining region cannot hold the request. */
    uint8_t* Allocate(size_t size);

    /* Caller may shorten the most recent allocation. The cursor is
       kept 4-byte aligned and any padding gap created by alignment
       is filled with a single NOP. */
    void FreeUnusedTail(uint8_t* start_of_free);

    /* Drop every allocation; reset the cursor to the start of the
       region. Pages stay committed - overcommit means the unused
       tail consumes no physical RAM. */
    void Flush();

    size_t MaxSize() const { return region_size_; }

private:
    static constexpr size_t kRegionSize = 32u * 1024u * 1024u;

    uint8_t* region_start_ = nullptr;
    uint8_t* cursor_       = nullptr;
    uint8_t* region_end_   = nullptr;
    size_t   region_size_  = 0;
};
