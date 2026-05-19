#pragma once

#include <cstddef>
#include <cstdint>

#include "jit_block.h"

class JitBlockIndex {
public:
    JitBlockIndex() = default;

    /* Wire the NIL sentinel and empty the root. */
    void Initialize();

    /* Per-record memory sizes — JitCompile multiplies by
       entrypoint_count and folds into the slab allocation size. */
    static constexpr size_t OuterEntrySize();
    static constexpr size_t SubEntrySize();

    JitBlock* PlaceOuterAt(void* memory, const JitBlock& block);

    /* Place a sub-entrypoint (bare JitBlock) at `memory`. `parent`
       is the outer entrypoint this sub belongs to; the new sub is
       prepended to parent->sub_block. Returns the placed JitBlock. */
    JitBlock* PlaceSubAt(void* memory, JitBlock* parent, const JitBlock& block);

    /* EPFromGuestAddr equivalent — find the outer entrypoint whose
       [guest_start, guest_end] range contains guest_addr, then walk
       its sub_block chain for an exact-PC match; fall back to the
       outer entrypoint on no sub match. */
    JitBlock* FindContaining(uint32_t guest_addr);

    /* EPFromGuestAddrExact equivalent — like FindContaining but
       returns nullptr unless the result's guest_start == guest_addr. */
    JitBlock* FindExact(uint32_t guest_addr);

    /* GetNextEPFromGuestAddr equivalent — first outer with
       guest_start > guest_addr. */
    JitBlock* FindNext(uint32_t guest_addr);

    /* IsGuestRangeInCache equivalent — any outer entry's range
       intersects [start_addr, end_addr]? */
    bool ContainsRange(uint32_t start_addr, uint32_t end_addr) const;

    /* FlushEntrypoints equivalent — forget all references. The
       backing memory is in the arena and is freed by arena flush;
       this method just resets the tree root to NIL. */
    void Flush();

    bool Empty() const;

private:
    enum class NodeColor : uint8_t { kRed, kBlack };

    /* EPNODE equivalent. JitBlock `ep` is the first field so a
       reinterpret_cast<JitBlock*>(node_memory) gives the embedded
       block — matching the reference's ((PENTRYPOINT)CodeLocation)
       cast. */
    struct Node {
        JitBlock   ep;
        Node*      left;
        Node*      right;
        Node*      parent;
        NodeColor  color;
    };

    Node          nil_storage_{};
    Node*         nil_  = nullptr;
    Node*         root_ = nullptr;

    /* CLR RB-tree primitives ("Introduction to Algorithms" Ch. 13). */
    Node* LeftRotate (Node* root, Node* x);
    Node* RightRotate(Node* root, Node* x);
    Node* TreeInsertBst(Node* root, Node* z);
    Node* RbInsert(Node* root, Node* z);
    Node* RbFind(Node* root, uint32_t addr) const;
    Node* RbFindNext(Node* root, uint32_t addr) const;
    bool  RbContainsRange(Node* root, uint32_t start_addr, uint32_t end_addr) const;
};

constexpr size_t JitBlockIndex::OuterEntrySize() {
    return sizeof(Node);
}

constexpr size_t JitBlockIndex::SubEntrySize() {
    return sizeof(JitBlock);
}
