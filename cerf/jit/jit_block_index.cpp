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

/* QEMU accel/tcg/cpu-exec.c:619 tb_add_jump(). */
void JitBlockIndex::LinkChain(JitBlock* src, uint32_t slot, JitBlock* dest,
                              uint8_t* site, uint8_t* fallback) {
    src->chain_site[slot]          = site;
    src->chain_fallback[slot]      = fallback;
    src->chain_target[slot]        = dest;
    src->chain_src_next[slot]      = dest->chain_src_head;
    src->chain_src_next_slot[slot] = dest->chain_src_head_slot;
    dest->chain_src_head           = src;
    dest->chain_src_head_slot      = static_cast<uint8_t>(slot);
}

/* QEMU accel/tcg/tb-maint.c:871 tb_reset_jump(). */
void JitBlockIndex::RestoreFallbackJump(JitBlock* src, uint32_t slot) {
    const uint32_t disp = static_cast<uint32_t>(
        src->chain_fallback[slot] - (src->chain_site[slot] + 4));
    std::memcpy(src->chain_site[slot], &disp, 4);
}

/* QEMU accel/tcg/tb-maint.c:812 tb_remove_from_jmp_list(). */
void JitBlockIndex::DetachFromDest(JitBlock* block, uint32_t slot) {
    JitBlock* const dest = block->chain_target[slot];
    if (dest == nullptr) return;

    RestoreFallbackJump(block, slot);

    JitBlock* cur      = dest->chain_src_head;
    uint8_t   cur_slot = dest->chain_src_head_slot;
    if (cur == block && cur_slot == slot) {
        dest->chain_src_head      = block->chain_src_next[slot];
        dest->chain_src_head_slot = block->chain_src_next_slot[slot];
    } else {
        while (cur != nullptr) {
            JitBlock* const nxt      = cur->chain_src_next[cur_slot];
            const uint8_t   nxt_slot = cur->chain_src_next_slot[cur_slot];
            if (nxt == block && nxt_slot == slot) {
                cur->chain_src_next[cur_slot]      = block->chain_src_next[slot];
                cur->chain_src_next_slot[cur_slot] = block->chain_src_next_slot[slot];
                break;
            }
            cur      = nxt;
            cur_slot = nxt_slot;
        }
    }
    block->chain_target[slot]   = nullptr;
    block->chain_src_next[slot] = nullptr;
}

/* QEMU accel/tcg/tb-maint.c:878 tb_jmp_unlink(). */
void JitBlockIndex::UnlinkChains(JitBlock* block) {
    DetachFromDest(block, 0u);
    DetachFromDest(block, 1u);

    JitBlock* src      = block->chain_src_head;
    uint8_t   src_slot = block->chain_src_head_slot;
    while (src != nullptr) {
        JitBlock* const nxt      = src->chain_src_next[src_slot];
        const uint8_t   nxt_slot = src->chain_src_next_slot[src_slot];
        RestoreFallbackJump(src, src_slot);
        src->chain_target[src_slot]   = nullptr;
        src->chain_src_next[src_slot] = nullptr;
        src      = nxt;
        src_slot = nxt_slot;
    }
    block->chain_src_head = nullptr;
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
    UnlinkChains(block);
    clear_jc(block->guest_start, ctx);
    blocks_by_start_.erase(it);
}
