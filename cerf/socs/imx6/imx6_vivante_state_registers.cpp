#include "imx6_vivante_mem.h"
#include "imx6_vivante_state_registers.h"

namespace imx6_vivante {

bool VivanteStateRegisters::SupportsOffset(uint32_t off) {
    if ((off & 3u) != 0u) return false;
    if ((off >= 0x01800u && off <= 0x01930u) ||
        (off >= 0x02800u && off <= 0x02930u) ||
        (off >= 0x02A00u && off <= 0x02B30u) ||
        (off >= 0x03400u && off <= 0x037FCu)) return true;
    static constexpr uint32_t multi_bases[] = {
        0x12800u, 0x12810u, 0x12820u, 0x12830u, 0x12840u,
        0x12850u, 0x12860u, 0x12870u, 0x12880u, 0x12890u,
        0x128E0u, 0x128F0u, 0x12900u, 0x12910u, 0x12920u,
        0x12930u, 0x12940u, 0x12950u, 0x12960u, 0x12970u,
    };
    for (uint32_t base : multi_bases)
        if (off >= base && off <= base + 0x0Cu) return true;
    switch (off) {
    case 0x007E8u: case 0x007F4u:
    case 0x01200u: case 0x01204u: case 0x01208u: case 0x0120Cu: case 0x01210u: case 0x01214u:
    case 0x01218u: case 0x0121Cu: case 0x01220u: case 0x01224u: case 0x01228u: case 0x0122Cu:
    case 0x01230u: case 0x01234u: case 0x01238u: case 0x0123Cu: case 0x01240u: case 0x01244u:
    case 0x01248u: case 0x0124Cu: case 0x01250u: case 0x01254u: case 0x0125Cu: case 0x01260u:
    case 0x01264u: case 0x01268u: case 0x01270u: case 0x01274u: case 0x0127Cu: case 0x01280u:
    case 0x01284u: case 0x01288u: case 0x0128Cu: case 0x01290u: case 0x01294u: case 0x01298u:
    case 0x0129Cu: case 0x012A0u: case 0x012A4u: case 0x012A8u: case 0x012ACu: case 0x012B4u:
    case 0x012B8u: case 0x012BCu: case 0x012C0u: case 0x012C4u: case 0x012C8u: case 0x012CCu:
    case 0x012D0u: case 0x012D4u: case 0x012D8u: case 0x012DCu: case 0x012E0u: case 0x012E4u:
    case 0x012F0u: case 0x01300u: case 0x01304u: case 0x01308u: case 0x0130Cu:
    case 0x01600u: case 0x01604u: case 0x01608u: case 0x0160Cu: case 0x01610u: case 0x01614u:
    case 0x01620u: case 0x0163Cu: case 0x01640u: case 0x01644u: case 0x01648u: case 0x0164Cu:
    case 0x01650u: case 0x01654u: case 0x01658u: case 0x0165Cu: case 0x01660u: case 0x016B0u:
    case 0x016C0u: case 0x016C4u: case 0x016E0u: case 0x016E4u: case 0x01700u: case 0x01704u:
    case 0x03800u: case 0x03804u: case 0x03808u: case 0x0380Cu: case 0x03C00u:
        return true;
    default:
        return false;
    }
}

void VivanteStateRegisters::Store(uint32_t byte_off, uint32_t value) {
    const uint32_t idx = byte_off >> 2;
    if (idx >= state_.state_.size()) {
        memory_.HaltUnsupported("imx6-vivante state store out of range", byte_off, value);
    }

    if (byte_off == 0x00004u) { /* VIVS_HI_IDLE_STATE */
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante write to IDLE_STATE", byte_off, value);
        return;
    }
    if (byte_off == 0x03800u) { /* VIVS_GL_PIPE_SELECT */
        if ((value & ~1u) != 0u) {
            memory_.HaltUnsupported("imx6-vivante unsupported PIPE_SELECT bits", byte_off, value);
        }
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x012E8u || byte_off == 0x012ECu) {
        /* etnaviv state_2d.xml PE_DITHER_{LOW,HIGH}: 0xFFFFFFFF disables dithering. */
        if (value != 0xFFFFFFFFu) memory_.HaltUnsupported("imx6-vivante dither", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x01324u) {
        /* hmi_ktp400_mobile_v13 writes 0x00030003; etnaviv state_2d.xml has no register at 0x1324. */
        if (value != 0x00030003u) memory_.HaltUnsupported("imx6-vivante state 0x1324", byte_off, value);
        return;
    }
    if (byte_off == 0x01720u) {
        /* etnaviv state_3d.xml TS.SAMPLER.CONFIG: zero leaves sampler tile status disabled. */
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante sampler tile status", byte_off, value);
        return;
    }
    if (byte_off == 0x03818u) {
        /* hmi_ktp400_mobile_v13 programs GL.MULTI_SAMPLE_CONFIG=2 on the GC320 2D core. */
        if (!memory_.Is2d() || value != 2u) memory_.HaltUnsupported("imx6-vivante multisample config", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x0130Cu) { /* VIVS_DE_DEYUV_CONVERSION */
        if (value != 0u) {
            memory_.HaltUnsupported("imx6-vivante unsupported DEYUV conversion", byte_off, value);
        }
        state_.state_[idx] = 0u;
        return;
    }

    if (byte_off == 0x0380Cu || /* VIVS_GL_FLUSH_CACHE */
        byte_off == 0x01650u) { /* VIVS_TS_FLUSH_CACHE */
        memory_.FlushEngineCaches();
        state_.state_[idx] = 0u;
        return;
    }

    if (byte_off == 0x03810u) {
        /* etnaviv state.xml GL.FLUSH_MMU defines flush bits 0..4. */
        if ((value & ~0x1Fu) != 0u)
            memory_.HaltUnsupported("imx6-vivante unsupported FLUSH_MMU bits", byte_off, value);
        return;
    }

    if (byte_off == 0x01654u) { /* VIVS_TS_MEM_CONFIG */
        memory_.HaltUnsupported("imx6-vivante TS_MEM_CONFIG write", byte_off, value);
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

    if ((byte_off >= 0x12930u && byte_off < 0x12940u) || byte_off == 0x012D4u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12},
            {0x03330000u, 1u << 28},
            {0x20000000u, 1u << 31},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if ((byte_off >= 0x12940u && byte_off < 0x12950u) || byte_off == 0x012D8u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},
            {0x00000010u, 1u << 7},
            {0x00000100u, 1u << 11},
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

    if ((byte_off >= 0x128F0u && byte_off < 0x12900u) || byte_off == 0x012BCu) {
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

    if (SupportsOffset(byte_off)) {
        state_.state_[idx] = value;
        return;
    }
    if (byte_off >= 0x12A60u && byte_off < 0x12A80u) {
        /* etnaviv state_2d.xml BLOCK8.SRC_CONFIG[0..7]; hmi_ktp400_mobile_v17 writes 0 to all eight. */
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante BLOCK8 source config", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    memory_.HaltUnsupported("imx6-vivante unmodelled state register", byte_off, value);
}

}
