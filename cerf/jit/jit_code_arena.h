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

    uint8_t* AllocatePermanent(size_t size);

    /* Caller may shorten the most recent allocation. The cursor is
       kept 4-byte aligned. */
    void FreeUnusedTail(uint8_t* start_of_free);

    void Flush();

    size_t MaxSize() const { return region_size_; }

    const uint8_t* RegionBase() const { return region_start_; }

    size_t Remaining() const {
        return static_cast<size_t>(region_end_ - cursor_);
    }

private:
    static constexpr size_t kRegionSize = 32u * 1024u * 1024u;

    uint8_t* region_start_ = nullptr;
    uint8_t* flush_floor_  = nullptr;
    uint8_t* cursor_       = nullptr;
    uint8_t* region_end_   = nullptr;
    size_t   region_size_  = 0;
};
