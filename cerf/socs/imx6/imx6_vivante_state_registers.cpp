#include "imx6_vivante_mem.h"
#include "imx6_vivante_state_registers.h"

namespace imx6_vivante {

void VivanteStateRegisters::Store(uint32_t byte_off, uint32_t value) {
    const uint32_t idx = byte_off >> 2;
    if (idx >= state_.state_.size()) return;

    if (byte_off == kMmuv2SafeAddress) {
        mmu_.WriteSafeAddress(value);
        state_.state_[idx] = state_.regs_[kMmuv2SafeAddress >> 2];
        return;
    }
    if (byte_off == kMmuv2Configuration) {
        mmu_.WriteConfiguration(value);
        state_.state_[idx] = state_.regs_[kMmuv2Configuration >> 2];
        return;
    }

    if (byte_off == 0x0380Cu || /* VIVS_GL_FLUSH_CACHE */
        byte_off == 0x01650u) { /* VIVS_TS_FLUSH_CACHE */
        memory_.FlushEngineCaches(value);
        state_.state_[idx] = 0u;
        return;
    }

    if (byte_off == 0x01654u) { /* VIVS_TS_MEM_CONFIG */
        state_.state_[idx] = memory_.SanitizeTileStatusConfig(value);
        return;
    }

    if (byte_off == 0x0123Cu) { /* VIVS_DE_PATTERN_CONFIG */
        state_.state_[idx] = value;
        const bool pattern = (value & (1u << 4)) != 0u;
        const uint32_t init_trigger = (value >> 6) & 3u;
        const uint32_t bpp = memory_.PatternBytesPerPixel(value & 0xFu);
        const uint32_t pat_addr = memory_.StateReg(0x01238u);
        state_.de_pattern_latch_valid_ = false;
        if (pattern && init_trigger != 0u && pat_addr != 0u && bpp != 0u) {
            const size_t bytes = static_cast<size_t>(8u * 8u * bpp);
            if (memory_.ReadGpuBytes(pat_addr, state_.de_pattern_latch_, bytes, MmuClient::Texture)) {
                state_.de_pattern_latch_config_ = value;
                state_.de_pattern_latch_address_ = pat_addr;
                state_.de_pattern_latch_bpp_ = bpp;
                state_.de_pattern_latch_valid_ = true;
            }
        }
        return;
    }

    if (byte_off == 0x03808u) { /* VIVS_GL_SEMAPHORE_TOKEN */
        state_.state_[idx] = value;
        memory_.ArmSemaphoreToken(value);
        return;
    }

    if (byte_off == 0x03C00u) { /* VIVS_GL_STALL_TOKEN */
        state_.state_[idx] = value;
        memory_.TryConsumeSemaphoreToken(value);
        return;
    }

    if (byte_off == 0x01294u) { /* VIVS_DE_VR_CONFIG */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* START */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012B0u) { /* VIVS_DE_PE_CONFIG */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* DESTINATION_FETCH */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x12930u && byte_off < 0x12940u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12},
            {0x03330000u, 1u << 28},
            {0x20000000u, 1u << 31},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x12940u && byte_off < 0x12950u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},
            {0x00000010u, 1u << 7},
            {0x00000100u, 1u << 11},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012D4u) { /* VIVS_DE_PE_TRANSPARENCY */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12}, /* SOURCE/PATTERN/DESTINATION */
            {0x03330000u, 1u << 28}, /* USE_SRC/PAT/DST_OVERRIDE */
            {0x20000000u, 1u << 31}, /* DFB_COLOR_KEY */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012D8u) { /* VIVS_DE_PE_CONTROL */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},  /* YUV matrix: BT.601/BT.709 */
            {0x00000010u, 1u << 7},  /* UV/VU swizzle */
            {0x00000100u, 1u << 11}, /* YUV -> RGB enable */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012E4u) { /* VIVS_DE_VR_CONFIG_EX */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3}, /* VERTICAL_LINE_WIDTH */
            {0x000000F0u, 1u << 8}, /* FILTER_TAP */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012F0u) { /* VIVS_DE_BW_CONFIG */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},  /* BLOCK_CONFIG */
            {0x00000010u, 1u << 7},  /* BLOCK_WALK_DIRECTION */
            {0x00000100u, 1u << 11}, /* TILE_WALK_DIRECTION */
            {0x00001000u, 1u << 15}, /* PIXEL_WALK_DIRECTION */
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off >= 0x128F0u && byte_off < 0x12900u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000007u, 1u << 8},
            {0x00000038u, 1u << 9},
            {0x00003000u, 1u << 15},
            {0x00030000u, 1u << 19},
        };
        uint32_t merged = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        if ((value & (1u << 8)) == 0u) merged |= 1u << 8;
        if ((value & (1u << 9)) == 0u) merged |= 1u << 9;
        if ((value & (1u << 15)) == 0u) merged |= 1u << 15;
        if ((value & (1u << 19)) == 0u) merged |= 1u << 19;
        state_.state_[idx] = merged;
        return;
    }

    if (byte_off == 0x012BCu) { /* VIVS_DE_ROT_ANGLE */
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000007u, 1u << 8},  /* SRC */
            {0x00000038u, 1u << 9},  /* DST */
            {0x00003000u, 1u << 15}, /* SRC_MIRROR */
            {0x00030000u, 1u << 19}, /* DST_MIRROR */
        };
        uint32_t merged = memory_.MergeMaskedState(state_.state_[idx], value, groups);

        if ((value & (1u << 8)) == 0u) merged |= 1u << 8;
        if ((value & (1u << 9)) == 0u) merged |= 1u << 9;
        if ((value & (1u << 15)) == 0u) merged |= 1u << 15;
        if ((value & (1u << 19)) == 0u) merged |= 1u << 19;
        state_.state_[idx] = merged;
        return;
    }

    state_.state_[idx] = value;
}

} // namespace imx6_vivante
