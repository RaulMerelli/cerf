#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"
#include "imx51_pixel_pack.h"
#include "imx51_gpu3d_regs.h"

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

using namespace imx51_gpu3d_regs;

class Imx51Gpu3d : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t a) override {
        switch ((a - kBase) >> 2) {
            case kIdxPmOverride1: return pm_override1_;
            case kIdxPmOverride2: return pm_override2_;
            case kIdxRbbmStatus:  return kRbbmStatusIdle;
            case kIdxMasterIntSignal: return 0u;
            case kIdxPeriphId1:    return 0u;
            case kIdxPeriphId2:    return 0u;
            case kIdxPatchRelease: return 0u;
            case kIdxRbCntl:       return rb_cntl_;
            case kIdxRbWptr:       return wptr_;  /* regread returns rb->wptr, gsl_yamato_imx.c:791 */
        }
        if (auto it = reg_file_.find((a - kBase) >> 2); it != reg_file_.end())
            return it->second;  /* a register the guest programmed via a TYPE0 write */
        if (((a - kBase) >> 2) == kIdxSqInstStoreManagment)
            return 0u;  /* read-before-write power-on default (reg_file_ serves it once a restore writes it) */
        HaltUnsupportedAccess("ReadWord", a, 0);
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        switch ((a - kBase) >> 2) {
            case kIdxPmOverride1: pm_override1_ = v; return;
            case kIdxPmOverride2: pm_override2_ = v; return;
            case kIdxSoftReset:       return;
            case kIdxRbbmCntl:        return;
            case kIdxRbbmIntCntl:     return;
            case kIdxCpIntCntl:       return;
            case kIdxRbWptrBase:      return;
            case kIdxRbWptrDelay:     return;
            case kIdxMhArbiterConfig: return;
            case kIdxSqVsProgram:     return;
            case kIdxSqPsProgram:     return;
            case kIdxMhMmuConfig:     return;
            case kIdxMhInterruptMask: return;
            case kIdxMhMmuMpuBase:    return;
            case kIdxMhMmuMpuEnd:     return;
            case kIdxRbCntl:          rb_cntl_ = v; return;
            /* CP/render config, ring-scan-inert. */
            case kIdxRbEdramInfo:
            case kIdxScratchAddr:
            case kIdxScratchUmsk:
            case kIdxCpIntAck:
            case kIdxCpDebug:
            case kIdxMeCntl:
            case kIdxMeRamWaddr:
            case kIdxMeRamData:
            case kIdxPfpUcodeAddr:
            case kIdxPfpUcodeData:
            case kIdxQueueThresh:     return;
            /* RB_BASE (re)inits the ring: reset the read/write cursor to match
               kgsl_ringbuffer_start's rb->rptr=rb->wptr=0 (kgsl_ringbuffer.c:419). */
            case kIdxRbBase:          rb_base_ = v; rptr_ = 0; wptr_ = 0; return;
            case kIdxRbRptrAddr:      rb_rptr_addr_ = v; return;
            case kIdxRbWptr:          HandleRbWptr(v); return;
        }
        HaltUnsupportedAccess("WriteWord", a, v);
    }

    void SaveState(StateWriter& w) override {
        w.Write(pm_override1_);
        w.Write(pm_override2_);
        w.Write(rb_cntl_);
        w.Write(rb_base_);
        w.Write(rb_rptr_addr_);
        w.Write(rptr_);
        w.Write(wptr_);
        w.Write(static_cast<uint32_t>(reg_file_.size()));
        for (const auto& [idx, val] : reg_file_) { w.Write(idx); w.Write(val); }
    }
    void RestoreState(StateReader& r) override {
        r.Read(pm_override1_);
        r.Read(pm_override2_);
        r.Read(rb_cntl_);
        r.Read(rb_base_);
        r.Read(rb_rptr_addr_);
        r.Read(rptr_);
        r.Read(wptr_);
        uint32_t n = 0;
        r.Read(n);
        reg_file_.clear();
        for (uint32_t k = 0; k < n; ++k) { uint32_t idx = 0, val = 0; r.Read(idx); r.Read(val); reg_file_[idx] = val; }
    }

private:
    uint32_t ReadPa32(uint32_t pa) {
        const uint8_t* hp = emu_.Get<EmulatedMemory>().TryTranslate(pa);
        if (!hp)
            HaltUnsupportedAccess("CP ring/IB read unbacked", pa, 0);
        return *reinterpret_cast<const uint32_t*>(hp);
    }

    /* TYPE0 register write (kgsl_pm4types.h:157): cnt data dwords -> consecutive regs
       regindx..regindx+cnt-1, or all to regindx when bit15 (same-register) is set.
       Held in the register file so the draw-context REG_TO_MEM save reads them back. */
    void StoreType0(uint32_t hdr, uint32_t pa, uint32_t cnt) {
        const uint32_t regindx = hdr & 0x7FFFu;
        const bool     same    = (hdr & 0x8000u) != 0u;
        for (uint32_t k = 0; k < cnt; ++k)
            reg_file_[same ? regindx : regindx + k] = ReadPa32(pa + 4u + k * 4u);
    }

    /* SET_CONSTANT (kgsl_drawctxt.c:360 PM4_REG / gsl_yamato_imx.c:286): first payload
       dword = (type<<16)|offset; the cnt-1 values load into that type's bank, which the
       draw-context save reads back as registers (reg_to_mem). Held in the register file. */
    void StoreSetConstant(uint32_t pa, uint32_t cnt) {
        const uint32_t tgt    = ReadPa32(pa + 4u);
        const uint32_t offset = tgt & 0xFFFFu;
        uint32_t base = 0u;
        switch ((tgt >> 16) & 0x7u) {
            case 0u: base = kScBaseAlu;   break;
            case 1u: base = kScBaseFetch; break;
            case 2u: base = kScBaseBool;  break;
            case 3u: base = kScBaseLoop;  break;
            case 4u: base = kScBaseReg;   break;
            default: HaltUnsupportedAccess("SET_CONSTANT type", pa, tgt);
        }
        for (uint32_t j = 0; j + 1u < cnt; ++j)
            reg_file_[base + offset + j] = ReadPa32(pa + 8u + j * 4u);
    }

    /* INDIRECT_BUFFER follow: drain context-state setup; draws FATAL (kgsl_pm4types.h). */
    void ScanIb(uint32_t ibaddr, uint32_t sizedwords) {
        for (uint32_t i = 0; i < sizedwords; ) {
            const uint32_t pa   = ibaddr + i * 4u;
            const uint32_t hdr  = ReadPa32(pa);
            const uint32_t type = hdr >> 30;
            const uint32_t cnt  = ((hdr >> 16) & 0x3FFFu) + 1u;
            if (type == kPm4Type0) { StoreType0(hdr, pa, cnt); i += 1u + cnt; continue; }
            if (type == kPm4Type2) { i += 1u; continue; }
            if (type != kPm4Type3)
                HaltUnsupportedAccess("IB packet type", pa, hdr);
            switch ((hdr >> 8) & 0xFFu) {
                case kPm4OpNop:
                case kPm4OpWaitForIdle:
                case kPm4OpInvalidateState: break;  /* invalidates GPU pipeline state groups so later draws reload; CERF's GPU3D caches no cross-draw state (each C2D blit reads its config fresh from reg_file_), so nothing to flush -> inert */
                case kPm4OpLoadConstantContext: break;  /* loads ALU/TEX from memory the save never reg_to_mem's; render draws FATAL -> inert */
                case kPm4OpImStore: break;  /* copies the (unmodeled) shader instruction memory to system memory (kgsl_pm4types.h:148), consumed only by a shader DRAW, which FATALs at HandleDrawIndx -> inert */
                case kPm4OpImLoad:          /* pointer-based (kgsl_pm4types.h:118) */
                case kPm4OpImLoadImmediate: break;  /* both load shader instruction memory (inline form kgsl_pm4types.h:121); the modeled C2D blit runs no shader (HandleDrawIndx = fixed-function copy) so it is never consumed -> inert */
                case kPm4OpSetShaderBases: break;  /* sets vertex/pixel shader instruction base pointers; the modeled C2D blit (HandleDrawIndx) is a fixed-function surface copy that runs no shader, so the bases are never consumed -> inert */
                case kPm4OpRegRmw: {  /* fixup RMW of SCRATCH_REG2; the operand it computes is read back only by SET_SHADER_BASES (0x4A), which is inert (fixed-function blit runs no shader) -> operand unused -> inert */
                    const uint32_t rmw_reg = ReadPa32(pa + 4u);
                    if (rmw_reg != kIdxScratchReg2)
                        HaltUnsupportedAccess("REG_RMW target", pa, rmw_reg);
                    break;
                }
                case kPm4OpWaitRegEq: {  /* [reg][ref][mask][poll] (lib2d-z430 emitter sub_41A62890); the Z430 completes synchronously, so the wait is met by the current register state, else self-reveal */
                    const uint32_t reg  = ReadPa32(pa + 4u);
                    const uint32_t ref  = ReadPa32(pa + 8u);
                    const uint32_t mask = ReadPa32(pa + 12u);
                    if ((ReadWord(kBase + reg * 4u) & mask) != ref)
                        HaltUnsupportedAccess("WAIT_REG_EQ condition unmet", pa, reg);
                    break;
                }
                case kPm4OpSetConstant: StoreSetConstant(pa, cnt); break;
                case kPm4OpRegToMem: HandleRegToMem(pa); break;
                case kPm4OpEventWrite: HandleEventWrite(pa); break;  /* blit-tail CACHE_FLUSH (cnt=1) / CACHE_FLUSH_TS */
                case kPm4OpDrawIndx: HandleDrawIndx(pa); break;
                default:
                    HaltUnsupportedAccess("IB opcode", pa, hdr);  /* unknown draw/opcode */
            }
            i += 1u + cnt;
        }
    }

    /* EVENT_WRITE/CACHE_FLUSH_TS: write the EOP timestamp the guest polls via
       kgsl_cmdstream_check_timestamp (kgsl_ringbuffer.c:635-640); addr+value inline. */
    void HandleEventWrite(uint32_t pa) {
        const uint32_t event = ReadPa32(pa + 4u);
        if (event == kEventCacheFlush) return;  /* no writeback; GPU MMU off -> DRAM already coherent */
        if (event != kEventCacheFlushTs)
            HaltUnsupportedAccess("CP EVENT_WRITE event", pa, event);
        const uint32_t addr = ReadPa32(pa + 8u);
        uint8_t* dst = emu_.Get<EmulatedMemory>().TryTranslateWrite(addr);
        if (!dst)
            HaltUnsupportedAccess("EOP timestamp writeback unbacked", addr, ReadPa32(pa + 12u));
        *reinterpret_cast<uint32_t*>(dst) = ReadPa32(pa + 12u);
    }

    /* REG_TO_MEM (draw-context save, kgsl_drawctxt.c reg_to_mem:416 /
       build_reg_to_mem_range:438): read GPU register `src` and write its value to
       memory at `dst`. Packet: [hdr cnt=2][src reg index (| shadow flag)][dst gpuaddr]. */
    void HandleRegToMem(uint32_t pa) {
        const uint32_t src   = ReadPa32(pa + 4u) & ~kRegToMemShadowFlag;
        const uint32_t dst   = ReadPa32(pa + 8u);
        const uint32_t value = ReadWord(kBase + src * 4u);  /* register-file / modeled read; unmodeled -> FATAL, self-revealing */
        uint8_t* out = emu_.Get<EmulatedMemory>().TryTranslateWrite(dst);
        if (!out)
            HaltUnsupportedAccess("REG_TO_MEM dst writeback unbacked", dst, value);
        *reinterpret_cast<uint32_t*>(out) = value;
    }

    uint32_t BlitReg(uint32_t idx, uint32_t pa) {
        auto it = reg_file_.find(idx);
        if (it == reg_file_.end())
            HaltUnsupportedAccess("blit config register not programmed", pa, idx);
        return it->second;
    }
    static float AsFloat(uint32_t u) { float f; std::memcpy(&f, &u, sizeof(f)); return f; }

    /* C2D2 2D-blit (lib2d-z430 sub_41A63F00): a 4-vertex screen-quad DRAW_INDX surface copy.
       The source read-swizzle (BGRA, SQ_TEX Z,Y,X,W) and the dest store-swap (RB_COLOR_INFO
       SWAP=1 = B8G8R8A8, mesa fd2_gmem.c fmt2swap) invert -> the copy is byte-identical 32bpp;
       adding any channel permutation here would double-swap and corrupt colors. */
    void HandleDrawIndx(uint32_t pa) {
        const uint32_t ctrl = ReadPa32(pa + 8u);  /* DRAW_INDX word2 (vgt_draw_initiator; a2xx num_indices[31:16]) */
        if ((ctrl & 0x3Fu) != 6u ||        /* PRIM_TYPE = 4-vertex quad (not kgsl's 3-vertex RectList) */
            ((ctrl >> 6) & 0x3u) != 2u ||  /* SOURCE_SELECT = AUTO_INDEX */
            (ctrl >> 16) != 4u)            /* num_indices = 4 */
            HaltUnsupportedAccess("DRAW_INDX not the C2D2 4-vert blit", pa, ctrl);

        /* dest surface: RB_COLOR_INFO (0x2001) FORMAT[3:0] = COLORX_8_8_8_8(5) or
           COLORX_5_6_5(2), SWAP[10:9]=1 (BGRA), BASE[31:12]; RB_SURFACE_INFO (0x2000)
           pitch[13:0] in pixels (a2xx.xml). */
        const uint32_t ci = BlitReg(0x2001u, pa);
        const uint32_t dstFmt = ci & 0xFu;
        if ((dstFmt != 5u && dstFmt != 2u) || ((ci >> 9) & 0x3u) != 1u)
            HaltUnsupportedAccess("blit dest not COLORX_8_8_8_8/5_6_5 SWAP=1", pa, ci);
        const uint32_t dstBase  = ci & 0xFFFFF000u;
        const uint32_t dstBpp   = (dstFmt == 2u) ? 2u : 4u;  /* COLORX_5_6_5=2B, _8_8_8_8=4B */
        const uint32_t dstPitch = BlitReg(0x2000u, pa) & 0x3FFFu;

        /* source surface = the SQ_TEX const (0x4800 + slot*6) whose base == the COHER-flushed
           source (COHER_BASE_PM4 0xA2A). a2xx.xml A2XX_SQ_TEX: w0 PITCH[30:22]<<5/TILED[31],
           w1 FORMAT[5:0]/BASE[31:12], w2 WIDTH[12:0]/HEIGHT[25:13] (size-1), w3 SWIZ_X/Y/Z/W +
           XY_MAG/MIN_FILTER[20:19]/[22:21]. */
        const uint32_t cohBase = BlitReg(0x0A2Au, pa);
        uint32_t fb = 0u;
        for (uint32_t s = 0u; s < 16u && fb == 0u; ++s) {
            auto it = reg_file_.find(0x4801u + s * 6u);  /* word1 carries the base */
            if (it != reg_file_.end() && (it->second & 0xFFFFF000u) == cohBase)
                fb = 0x4800u + s * 6u;
        }
        if (fb == 0u)
            HaltUnsupportedAccess("blit source fetch const not found", pa, cohBase);
        const uint32_t sw0 = BlitReg(fb + 0u, pa), sw1 = BlitReg(fb + 1u, pa);
        const uint32_t sw2 = BlitReg(fb + 2u, pa), sw3 = BlitReg(fb + 3u, pa);
        const uint32_t srcFmt = sw1 & 0x3Fu;      /* SQ_TEX FORMAT (a2xx_sq_surfaceformat) */
        const uint32_t swizW  = (sw3 >> 10) & 0x7u;
        if ((srcFmt != 6u && srcFmt != 4u) || (sw0 >> 31) != 0u ||  /* FMT_8_8_8_8/5_6_5, not tiled */
            ((sw3 >> 19) & 0x3u) != 0u || ((sw3 >> 21) & 0x3u) != 0u ||  /* XY_MAG/MIN_FILTER = POINT */
            ((sw3 >> 1) & 0x7u) != 2u || ((sw3 >> 4) & 0x7u) != 1u ||    /* SWIZ_X=Z, SWIZ_Y=Y */
            ((sw3 >> 7) & 0x7u) != 0u ||                                 /* SWIZ_Z=X (BGRA) */
            (swizW != 3u && swizW != 5u)) {  /* SWIZ_W = W (pass) or ONE (force opaque), a2xx.xml:1747 */
            HaltUnsupportedAccess("blit source not FMT_8888/565 POINT BGRA", pa, sw3);
        }
        const uint32_t srcBpp = (srcFmt == 4u) ? 2u : 4u;  /* FMT_5_6_5=2B, FMT_8_8_8_8=4B */
        /* SWIZ_W=ONE forces the sampled alpha opaque; it changes the stored pixel only for an
           alpha-bearing dest. Into 565 alpha is dropped (no-op); into 8888 it must write A=0xFF
           (not the source alpha) - not yet modeled, so FATAL. */
        if (swizW == 5u && dstBpp == 4u)
            HaltUnsupportedAccess("blit SWIZ_W=ONE into 8888 dest (alpha-force not modeled)", pa, sw3);
        const uint32_t srcBase  = sw1 & 0xFFFFF000u;
        const uint32_t srcPitch = ((sw0 >> 22) & 0x1FFu) << 5;
        const uint32_t srcW     = (sw2 & 0x1FFFu) + 1u;
        const uint32_t srcH     = ((sw2 >> 13) & 0x1FFFu) + 1u;

        /* opaque (RB_COLORCONTROL 0x2202 BLEND_DISABLE bit5) + all channels (RB_COLOR_MASK 0x2104
           == 0xF) + direct screen coords (PA_CL_VTE_CNTL 0x2206 viewport scale/offset [5:0] = 0). */
        if (((BlitReg(0x2202u, pa) >> 5) & 0x1u) != 1u ||
            (BlitReg(0x2104u, pa) & 0xFu) != 0xFu ||
            (BlitReg(0x2206u, pa) & 0x3Fu) != 0u)
            HaltUnsupportedAccess("blit not opaque/full-mask/direct-coord", pa, ci);

        /* geometry: vertex ALU 0x4048 = (W/2,H/2,W/2,H/2) -> 1:1 with source, origin (0,0);
           tex ALU 0x4098 = (0.5,0.5,0.5,0.5) -> full [0,1] source; window scissor (0x2081/0x2082,
           adreno_reg_xy X[14:0]/Y[30:16]) origin (0,0) covering the full extent (no clip). */
        const uint32_t vhw = BlitReg(0x4048u, pa), vhh = BlitReg(0x4049u, pa);
        if (AsFloat(vhw) * 2.0f != static_cast<float>(srcW) ||
            AsFloat(vhh) * 2.0f != static_cast<float>(srcH) ||
            BlitReg(0x404Au, pa) != vhw || BlitReg(0x404Bu, pa) != vhh)
            HaltUnsupportedAccess("blit geometry not 1:1 full-screen", pa, srcW);
        for (uint32_t k = 0u; k < 4u; ++k)
            if (BlitReg(0x4098u + k, pa) != 0x3F000000u)  /* 0.5f */
                HaltUnsupportedAccess("blit tex not full [0,1]", pa, 0x4098u + k);
        const uint32_t tl = BlitReg(0x2081u, pa), br = BlitReg(0x2082u, pa);
        if ((tl & 0x7FFFu) != 0u || ((tl >> 16) & 0x7FFFu) != 0u ||
            (br & 0x7FFFu) < srcW || ((br >> 16) & 0x7FFFu) < srcH)
            HaltUnsupportedAccess("blit scissor origin/clip", pa, br);

        /* Same-format C2D copy source[0..W,0..H] -> dest[0..W,0..H] (gpuaddr==physical). Validate
           each surface is one contiguously-backed span (start + last byte), then row-copy per bpp. */
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t sSpan = (srcH - 1u) * srcPitch * srcBpp + srcW * srcBpp;
        const uint32_t dSpan = (srcH - 1u) * dstPitch * dstBpp + srcW * dstBpp;
        const uint8_t* s0 = mem.TryTranslate(srcBase);
        const uint8_t* sN = mem.TryTranslate(srcBase + sSpan - 1u);
        uint8_t*       d0 = mem.TryTranslateWrite(dstBase);
        uint8_t*       dN = mem.TryTranslateWrite(dstBase + dSpan - 1u);
        if (!s0 || !d0 || sN != s0 + (sSpan - 1u) || dN != d0 + (dSpan - 1u))
            HaltUnsupportedAccess("blit surface not contiguously backed", srcBase, dstBase);
        if (srcBpp == dstBpp) {  /* same format: byte-identical row copy (swizzle+SWAP net identity) */
            for (uint32_t y = 0u; y < srcH; ++y)
                std::memcpy(d0 + y * dstPitch * dstBpp, s0 + y * srcPitch * srcBpp, srcW * srcBpp);
        } else if (srcBpp == 4u) {  /* 8888 source -> 565 dest: pack each 0xAARRGGBB pixel to standard
                                       RGB565 (the IPU BG scanout reads it back via Expand565). */
            for (uint32_t y = 0u; y < srcH; ++y) {
                const uint32_t* srow = reinterpret_cast<const uint32_t*>(s0 + y * srcPitch * 4u);
                uint16_t* drow = reinterpret_cast<uint16_t*>(d0 + y * dstPitch * 2u);
                for (uint32_t x = 0u; x < srcW; ++x) drow[x] = imx51_pixel::PackArgb565(srow[x]);
            }
        } else {  /* 565 source -> 8888 dest: not yet fired */
            HaltUnsupportedAccess("blit 565 source into 8888 dest (expand not modeled)", srcBase, dstBase);
        }
    }

    /* CP ring kick: scan the pending ring commands as PM4 packets, model the completion,
       then write rptr to the memptrs slot so kgsl_yamato_idle's rptr==wptr poll passes. */
    void HandleRbWptr(uint32_t wptr) {
        /* On wrap only [0,wptr) holds new commands: the driver NOP-pads the tail and
           submits it via a separate wptr=old_wptr+1 write this handler already consumed,
           then sets wptr=0 (kgsl_ringbuffer.c:211-221). rptr_ resets to the ring start. */
        if (wptr < rptr_)
            rptr_ = 0u;
        ScanRing(rptr_, wptr);
        rptr_ = wptr;
        wptr_ = wptr;
        uint8_t* rp = emu_.Get<EmulatedMemory>().TryTranslateWrite(rb_rptr_addr_);
        if (!rp)
            HaltUnsupportedAccess("CP rptr writeback unbacked", rb_rptr_addr_, wptr);
        *reinterpret_cast<uint32_t*>(rp) = wptr;
    }

    void ScanRing(uint32_t off, uint32_t end) {
        for (; off < end; ) {
            const uint32_t pa   = rb_base_ + off * 4u;
            const uint32_t hdr  = ReadPa32(pa);
            const uint32_t type = hdr >> 30;
            const uint32_t cnt  = ((hdr >> 16) & 0x3FFFu) + 1u;
            if (type == kPm4Type0) { off += 1u + cnt; continue; }  /* CP_TIMESTAMP */
            if (type == kPm4Type2) { off += 1u; continue; }
            if (type != kPm4Type3)
                HaltUnsupportedAccess("CP ring packet type", pa, hdr);
            switch ((hdr >> 8) & 0xFFu) {
                case kPm4OpMeInit:
                case kPm4OpNop:
                case kPm4OpWaitForIdle: break;
                case kPm4OpIndirectBuffer:
                case kPm4OpIndirectBufferPfd:
                    ScanIb(ReadPa32(pa + 4u), ReadPa32(pa + 8u));
                    break;
                case kPm4OpEventWrite: HandleEventWrite(pa); break;
                /* CP INTERRUPT: no ARM CP-completion line (RM Table 3-2, GPU3D=IRQ102 idle only). */
                case kPm4OpInterrupt:  break;
                default:
                    HaltUnsupportedAccess("CP ring-scan opcode", pa, hdr);
            }
            off += 1u + cnt;
        }
    }

    uint32_t pm_override1_ = 0;
    uint32_t pm_override2_ = 0;
    uint32_t rb_cntl_      = 0;
    uint32_t rb_base_      = 0;
    uint32_t rb_rptr_addr_ = 0;
    uint32_t rptr_         = 0;
    uint32_t wptr_         = 0;
    std::unordered_map<uint32_t, uint32_t> reg_file_;  /* GPU3D registers/constants the guest programs (TYPE0 / SET_CONSTANT), served on a REG_TO_MEM save */
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpu3d);
