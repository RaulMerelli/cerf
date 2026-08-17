#include "imx6_vivante_mem.h"

namespace imx6_vivante {

void VivanteMem::StoreStateReg(uint32_t byte_off, uint32_t value) {
    const uint32_t idx = byte_off >> 2;
    if (idx >= s_.state_.size()) return;

    /* HI/MMUv2 registers are part of the LOAD_STATE address space too.
       WinCE libGAL programs CONFIGURATION and SAFE_ADDRESS through the FE,
       then enables the MMU with the CPU MMIO CONTROL register.  Keep the
       live HI register file and the command-state mirror coherent. */
    if (byte_off == kMmuv2SafeAddress) {
        WriteMmuv2SafeAddress(value);
        s_.state_[idx] = s_.regs_[kMmuv2SafeAddress >> 2];
        return;
    }
    if (byte_off == kMmuv2Configuration) {
        WriteMmuv2Configuration(value);
        s_.state_[idx] = s_.regs_[kMmuv2Configuration >> 2];
        return;
    }

    if (byte_off == 0x0380Cu || /* VIVS_GL_FLUSH_CACHE */
        byte_off == 0x01650u) { /* VIVS_TS_FLUSH_CACHE */
        FlushEngineCaches(value);
        s_.state_[idx] = 0u; /* pulse/self-clearing register */
        return;
    }

    if (byte_off == 0x01654u) { /* VIVS_TS_MEM_CONFIG */
        s_.state_[idx] = SanitizeTileStatusConfig(value);
        return;
    }

    if (byte_off == 0x0123Cu) { /* VIVS_DE_PATTERN_CONFIG */
        s_.state_[idx] = value;
        /* etnaviv rnndb/state_2d.xml PATTERN_CONFIG.INIT_TRIGGER;
           KTP400 libGAL sub_42959634 writes PATTERN_ADDRESS (0x1238)
           and then PATTERN_CONFIG with INIT_TRIGGER=3 (0x...D0) to load
           a color brush.  Capture the repeated 8x8 pattern at the trigger
           write; later DRAW_2D consumes the hardware pattern latch, not a
           speculative reread of mutable guest memory. */
        const bool pattern = (value & (1u << 4)) != 0u;
        const uint32_t init_trigger = (value >> 6) & 3u;
        const uint32_t bpp = PatternBytesPerPixel(value & 0xFu);
        const uint32_t pat_addr = StateReg(0x01238u);
        s_.de_pattern_latch_valid_ = false;
        if (pattern && init_trigger != 0u && pat_addr != 0u && bpp != 0u) {
            const size_t bytes = static_cast<size_t>(8u * 8u * bpp);
            if (ReadGpuBytes(pat_addr, s_.de_pattern_latch_, bytes,
                             MmuClient::Texture)) {
                s_.de_pattern_latch_config_ = value;
                s_.de_pattern_latch_address_ = pat_addr;
                s_.de_pattern_latch_bpp_ = bpp;
                s_.de_pattern_latch_valid_ = true;
            }
        }
        return;
    }

    if (byte_off == 0x03808u) { /* VIVS_GL_SEMAPHORE_TOKEN */
        s_.state_[idx] = value;
        /* Engine work is executed synchronously by this model.  Therefore
           all work preceding the semaphore state is complete when this
           write is observed and the exact FROM/TO token can be armed. */
        ArmSemaphoreToken(value);
        return;
    }

    if (byte_off == 0x03C00u) { /* VIVS_GL_STALL_TOKEN */
        s_.state_[idx] = value;
        /* State-based stalls synchronize internal modules.  With the
           synchronous engine model a matching semaphore is already ready;
           consume it so a later FE stall cannot incorrectly reuse it. */
        TryConsumeSemaphoreToken(value);
        return;
    }

    if (byte_off == 0x01294u) { /* VIVS_DE_VR_CONFIG */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* START */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012B0u) { /* VIVS_DE_PE_CONFIG */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* DESTINATION_FETCH */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x12930u && byte_off < 0x12940u) {
        /* VIVS_DE_BLOCK4_TRANSPARENCY[i]: same active-low groups as the
           single-source PE_TRANSPARENCY register. */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12},
            {0x03330000u, 1u << 28},
            {0x20000000u, 1u << 31},
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x12940u && byte_off < 0x12950u) {
        /* VIVS_DE_BLOCK4_CONTROL[i]: per-source YUV control. */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},
            {0x00000010u, 1u << 7},
            {0x00000100u, 1u << 11},
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012D4u) { /* VIVS_DE_PE_TRANSPARENCY */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12}, /* SOURCE/PATTERN/DESTINATION */
            {0x03330000u, 1u << 28}, /* USE_SRC/PAT/DST_OVERRIDE */
            {0x20000000u, 1u << 31}, /* DFB_COLOR_KEY */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012D8u) { /* VIVS_DE_PE_CONTROL */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},  /* YUV matrix: BT.601/BT.709 */
            {0x00000010u, 1u << 7},  /* UV/VU swizzle */
            {0x00000100u, 1u << 11}, /* YUV -> RGB enable */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012E4u) { /* VIVS_DE_VR_CONFIG_EX */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* VERTICAL_LINE_WIDTH */
            {0x000000F0u, 1u << 8}, /* FILTER_TAP */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012F0u) { /* VIVS_DE_BW_CONFIG */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},  /* BLOCK_CONFIG */
            {0x00000010u, 1u << 7},  /* BLOCK_WALK_DIRECTION */
            {0x00000100u, 1u << 11}, /* TILE_WALK_DIRECTION */
            {0x00001000u, 1u << 15}, /* PIXEL_WALK_DIRECTION */
        };
        s_.state_[idx] = MergeMaskedState(s_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x128F0u && byte_off < 0x12900u) {
        /* VIVS_DE_BLOCK4_ROT_ANGLE[i].  Preserve the same internal valid
           bits used by the single-source renderer after active-low merge. */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000007u, 1u << 8},
            {0x00000038u, 1u << 9},
            {0x00003000u, 1u << 15},
            {0x00030000u, 1u << 19},
        };
        uint32_t merged = MergeMaskedState(s_.state_[idx], value, groups);
        if ((value & (1u << 8)) == 0u)  merged |= 1u << 8;
        if ((value & (1u << 9)) == 0u)  merged |= 1u << 9;
        if ((value & (1u << 15)) == 0u) merged |= 1u << 15;
        if ((value & (1u << 19)) == 0u) merged |= 1u << 19;
        s_.state_[idx] = merged;
        return;
    }

    if (byte_off == 0x012BCu) { /* VIVS_DE_ROT_ANGLE */
        static constexpr MaskedStateGroup groups[] = {
            {0x00000007u, 1u << 8},  /* SRC */
            {0x00000038u, 1u << 9},  /* DST */
            {0x00003000u, 1u << 15}, /* SRC_MIRROR */
            {0x00030000u, 1u << 19}, /* DST_MIRROR */
        };
        uint32_t merged = MergeMaskedState(s_.state_[idx], value, groups);

        /* The renderer also needs to know whether each ROT_ANGLE field was
           explicitly programmed, so reuse the otherwise transient mask
           positions as internal valid bits. */
        if ((value & (1u << 8)) == 0u)  merged |= 1u << 8;
        if ((value & (1u << 9)) == 0u)  merged |= 1u << 9;
        if ((value & (1u << 15)) == 0u) merged |= 1u << 15;
        if ((value & (1u << 19)) == 0u) merged |= 1u << 19;
        s_.state_[idx] = merged;
        return;
    }

    s_.state_[idx] = value;
}

void VivanteMem::WriteMmuv2Configuration(uint32_t value) {
    InvalidateTranslationCache();
    uint32_t cur = s_.regs_[kMmuv2Configuration >> 2];
    /* Active-low masked register: mask=1 preserves the corresponding
       field. FLUSH is a command bit and self-clears in this direct-table
       model because no software TLB cache is retained. */
    if ((value & (1u << 3)) == 0u)
        cur = (cur & ~1u) | (value & 1u);
    if ((value & (1u << 7)) == 0u)
        cur &= ~(1u << 4);
    if ((value & (1u << 8)) == 0u)
        cur = (cur & ~0xFFFFFC00u) | (value & 0xFFFFFC00u);
    s_.regs_[kMmuv2Configuration >> 2] = cur;
    const uint32_t idx = kMmuv2Configuration >> 2;
    if (idx < s_.state_.size())
        s_.state_[idx] = cur;
}

void VivanteMem::WriteMmuv2SafeAddress(uint32_t value) {
    /* rnndb/state_hi.xml: SAFE_ADDRESS is a 64-byte window and can only
       be programmed once after reset. Low six address bits are ignored. */
    if (s_.mmu_safe_address_written_)
        return;
    s_.regs_[kMmuv2SafeAddress >> 2] = value & ~0x3Fu;
    s_.mmu_safe_address_written_ = true;
    const uint32_t idx = kMmuv2SafeAddress >> 2;
    if (idx < s_.state_.size())
        s_.state_[idx] = s_.regs_[idx];
}

void VivanteMem::ResetMmuv2State() {
    InvalidateTranslationCache();
    s_.regs_[kMmuv2SafeAddress >> 2] = 0u;
    s_.regs_[kMmuv2Configuration >> 2] = 0u;
    s_.regs_[kMmuv2Status >> 2] = 0u;
    s_.regs_[kMmuv2Control >> 2] = 0u;
    for (uint32_t i = 0u; i < 4u; ++i)
        s_.regs_[(kMmuv2ExceptionAddress >> 2) + i] = 0u;
    s_.mmu_safe_address_written_ = false;
    if ((kMmuv2Configuration >> 2) < s_.state_.size()) {
        s_.state_[kMmuv2SafeAddress >> 2] = 0u;
        s_.state_[kMmuv2Configuration >> 2] = 0u;
    }
    s_.intr_status_ &= ~kMmuv2Interrupt;
    UpdateIrq();
}

}  // namespace imx6_vivante
