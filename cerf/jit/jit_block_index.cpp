#include "jit_block_index.h"

#include <cstring>
#include <new>

void JitBlockIndex::Initialize() {
    nil_ = &nil_storage_;
    nil_->left   = nil_;
    nil_->right  = nil_;
    nil_->parent = nil_;
    nil_->color  = NodeColor::kBlack;
    root_ = nil_;
}

JitBlock* JitBlockIndex::PlaceOuterAt(void* memory, const JitBlock& block) {
    Node* node = new (memory) Node{};
    node->ep            = block;
    node->ep.sub_block  = nullptr;
    node->left          = nil_;
    node->right         = nil_;
    node->parent        = nil_;
    node->color         = NodeColor::kRed;
    root_ = RbInsert(root_, node);
    return &node->ep;
}

JitBlock* JitBlockIndex::PlaceSubAt(void* memory, JitBlock* parent, const JitBlock& block) {
    JitBlock* sub = new (memory) JitBlock{block};
    sub->sub_block      = parent->sub_block;
    parent->sub_block   = sub;
    return sub;
}

JitBlock* JitBlockIndex::FindContaining(uint32_t guest_addr) {
    Node* node = RbFind(root_, guest_addr);
    if (!node) return nullptr;
    JitBlock* ep = &node->ep;
    do {
        if (ep->guest_start == guest_addr) return ep;
        ep = ep->sub_block;
    } while (ep);
    /* No sub-entrypoint matches exactly — return the outer block. */
    return &node->ep;
}

JitBlock* JitBlockIndex::FindExact(uint32_t guest_addr) {
    JitBlock* hit = FindContaining(guest_addr);
    if (hit && hit->guest_start == guest_addr) return hit;
    return nullptr;
}

JitBlock* JitBlockIndex::FindNext(uint32_t guest_addr) {
    Node* node = RbFindNext(root_, guest_addr);
    if (!node || node == nil_) return nullptr;
    return &node->ep;
}

bool JitBlockIndex::ContainsRange(uint32_t start_addr, uint32_t end_addr) const {
    if (root_ == nil_) return false;
    return RbContainsRange(root_, start_addr, end_addr);
}

void JitBlockIndex::Flush() {
    /* Arena owns the node memory; caller flushes the arena to
       actually free it. We just forget every reference. */
    root_ = nil_;
}

bool JitBlockIndex::Empty() const {
    return root_ == nil_;
}

JitBlockIndex::Node* JitBlockIndex::LeftRotate(Node* root, Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != nil_) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
    return root;
}

JitBlockIndex::Node* JitBlockIndex::RightRotate(Node* root, Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right != nil_) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_) {
        root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->right = x;
    x->parent = y;
    return root;
}

JitBlockIndex::Node* JitBlockIndex::TreeInsertBst(Node* root, Node* z) {
    Node* y = nil_;
    Node* x = root;
    z->left  = nil_;
    z->right = nil_;
    while (x != nil_) {
        y = x;
        if (z->ep.guest_start < x->ep.guest_start) {
            x = x->left;
        } else {
            x = x->right;
        }
    }
    z->parent = y;
    if (y == nil_) {
        root = z;
    } else if (z->ep.guest_start < y->ep.guest_start) {
        y->left = z;
    } else {
        y->right = z;
    }
    return root;
}

JitBlockIndex::Node* JitBlockIndex::RbInsert(Node* root, Node* x) {
    root = TreeInsertBst(root, x);
    x->color = NodeColor::kRed;

    while (x != root && x->parent->color == NodeColor::kRed) {
        if (x->parent == x->parent->parent->left) {
            Node* y = x->parent->parent->right;
            if (y->color == NodeColor::kRed) {
                x->parent->color           = NodeColor::kBlack;
                y->color                   = NodeColor::kBlack;
                x->parent->parent->color   = NodeColor::kRed;
                x = x->parent->parent;
            } else if (x == x->parent->right) {
                x = x->parent;
                root = LeftRotate(root, x);
            } else {
                x->parent->color           = NodeColor::kBlack;
                x->parent->parent->color   = NodeColor::kRed;
                root = RightRotate(root, x->parent->parent);
            }
        } else {
            Node* y = x->parent->parent->left;
            if (y->color == NodeColor::kRed) {
                x->parent->color           = NodeColor::kBlack;
                y->color                   = NodeColor::kBlack;
                x->parent->parent->color   = NodeColor::kRed;
                x = x->parent->parent;
            } else if (x == x->parent->left) {
                x = x->parent;
                root = RightRotate(root, x);
            } else {
                x->parent->color           = NodeColor::kBlack;
                x->parent->parent->color   = NodeColor::kRed;
                root = LeftRotate(root, x->parent->parent);
            }
        }
    }
    root->color = NodeColor::kBlack;
    return root;
}

JitBlockIndex::Node* JitBlockIndex::RbFind(Node* root, uint32_t addr) const {
    while (root != nil_) {
        if (addr < root->ep.guest_start) {
            root = root->left;
        } else if (addr > root->ep.guest_end) {
            root = root->right;
        } else {
            return root;
        }
    }
    return nullptr;
}

JitBlockIndex::Node* JitBlockIndex::RbFindNext(Node* root, uint32_t addr) const {
    if (root == nil_) return nullptr;
    Node* candidate = nullptr;
    while (root != nil_) {
        candidate = root;
        if (addr < root->ep.guest_start) {
            root = root->left;
        } else {
            root = root->right;
        }
    }
    while (candidate && addr > candidate->ep.guest_start) {
        if (candidate->parent == nil_) return nullptr;
        candidate = candidate->parent;
    }
    return candidate;
}

bool JitBlockIndex::RbContainsRange(Node* root, uint32_t start_addr, uint32_t end_addr) const {
    while (root != nil_) {
        if (start_addr <= root->ep.guest_start && root->ep.guest_start <= end_addr) return true;
        if (start_addr <= root->ep.guest_end   && root->ep.guest_end   <= end_addr) return true;
        if (root->ep.guest_start <= start_addr && end_addr <= root->ep.guest_end)   return true;

        if (start_addr < root->ep.guest_start) {
            root = root->left;
        } else {
            root = root->right;
        }
    }
    return false;
}
