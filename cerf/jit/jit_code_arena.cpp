#include "jit_code_arena.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../core/log.h"

JitCodeArena::~JitCodeArena() {
    if (region_start_) {
        VirtualFree(region_start_, 0, MEM_RELEASE);
    }
}

void JitCodeArena::Initialize() {
    if (region_start_) {
        LOG(Caution, "JitCodeArena::Initialize called twice\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    void* region = VirtualAlloc(nullptr, kRegionSize, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!region) {
        LOG(Caution, "JitCodeArena::Initialize: VirtualAlloc(%zu) failed\n",
            kRegionSize);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    region_start_ = static_cast<uint8_t*>(region);
    flush_floor_  = region_start_;
    cursor_       = region_start_;
    region_end_   = region_start_ + kRegionSize;
    region_size_  = kRegionSize;
}

uint8_t* JitCodeArena::Allocate(size_t size) {
    const size_t aligned = (size + 3u) & ~static_cast<size_t>(3u);
    if (aligned < size) {
        LOG(Caution, "JitCodeArena::Allocate: size overflow (%zu)\n", size);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (static_cast<size_t>(region_end_ - cursor_) < aligned) {
        return nullptr;
    }
    uint8_t* p = cursor_;
    cursor_ += aligned;
    return p;
}

void JitCodeArena::FreeUnusedTail(uint8_t* start_of_free) {
    if (start_of_free < flush_floor_ || start_of_free > cursor_) {
        LOG(Caution, "JitCodeArena::FreeUnusedTail: %p outside [%p, %p]\n",
            start_of_free, flush_floor_, cursor_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    cursor_ = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(start_of_free) + 3u) &
        ~static_cast<uintptr_t>(3u));
}

uint8_t* JitCodeArena::AllocatePermanent(size_t size) {
    uint8_t* p = Allocate(size);
    flush_floor_ = cursor_;
    return p;
}

void JitCodeArena::Flush() {
    cursor_ = flush_floor_;
}
