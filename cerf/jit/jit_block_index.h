#pragma once

#include <cstddef>
#include <cstdint>
#include <map>

#include "jit_block.h"

class JitBlockIndex {
public:
    using ClearJumpCacheFn = void (*)(uint32_t folded_va, void* ctx);

    void Initialize();
    void Flush();

    static size_t OuterEntrySize() {
        return (sizeof(JitBlock) + 15u) & ~static_cast<size_t>(15u);
    }

    JitBlock* PlaceOuterAt(uint8_t* slab, const JitBlock& block);

    JitBlock* FindExact(uint32_t guest_start);

    bool ContainsRange(uint32_t start, uint32_t end) const;

    void RemoveNode(JitBlock* block, ClearJumpCacheFn clear_jc, void* ctx);

private:
    std::map<uint32_t, JitBlock*> blocks_by_start_;

    uint32_t max_span_ = 0;
};
