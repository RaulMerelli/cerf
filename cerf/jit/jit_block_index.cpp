#include "jit_block_index.h"

#include <cstring>

#include "../core/log.h"

void JitBlockIndex::Initialize() {
    Flush();
}

void JitBlockIndex::Flush() {
    blocks_by_start_.clear();
    max_span_ = 0;
}

JitBlock* JitBlockIndex::PlaceOuterAt(uint8_t* slab, const JitBlock& block) {
    JitBlock* stored = reinterpret_cast<JitBlock*>(slab);
    std::memcpy(stored, &block, sizeof(JitBlock));

    const auto ins = blocks_by_start_.emplace(stored->guest_start, stored);
    if (!ins.second) {
        LOG(Caution, "JitBlockIndex::PlaceOuterAt: duplicate guest_start "
                "0x%08X (existing block was not evicted)\n",
            stored->guest_start);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const uint32_t span = stored->guest_end - stored->guest_start;
    if (span > max_span_) max_span_ = span;
    return stored;
}

JitBlock* JitBlockIndex::FindExact(uint32_t guest_start) {
    const auto it = blocks_by_start_.find(guest_start);
    return it != blocks_by_start_.end() ? it->second : nullptr;
}

bool JitBlockIndex::ContainsRange(uint32_t start, uint32_t end) const {
    const uint32_t scan_lo = (start > max_span_) ? (start - max_span_) : 0u;
    for (auto it = blocks_by_start_.lower_bound(scan_lo);
         it != blocks_by_start_.end() && it->first <= end; ++it) {
        if (it->second->guest_end >= start) return true;
    }
    return false;
}

void JitBlockIndex::RemoveNode(JitBlock* block, ClearJumpCacheFn clear_jc,
                               void* ctx) {
    const auto it = blocks_by_start_.find(block->guest_start);
    if (it == blocks_by_start_.end() || it->second != block) {
        LOG(Caution, "JitBlockIndex::RemoveNode: block 0x%08X..0x%08X is not "
                "the indexed record for its guest_start\n",
            block->guest_start, block->guest_end);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    clear_jc(block->guest_start, ctx);
    blocks_by_start_.erase(it);
}
