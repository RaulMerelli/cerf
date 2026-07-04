#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"
#if CERF_DEV_MODE
#include "../../core/log.h"
#endif

#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {

/* AMD Z430 GPU3D register block. MCIMX51RM Table 38-4: GC register space
   0x3000_0000..0x3002_FFFF, size 0x30000. */
constexpr uint32_t kBase = 0x30000000u;
constexpr uint32_t kSize = 0x00030000u;

/* RBBM_PM_OVERRIDE1/2 (0x39C/0x39D): clock-gating override config. R/W
   store+readback - the guest reads pm_override1 back (kgsl_drawctxt.c:479),
   so a write-only accept would corrupt its context save. */
constexpr uint32_t kIdxPmOverride1 = 0x039Cu;
constexpr uint32_t kIdxPmOverride2 = 0x039Du;

/* RBBM_SOFT_RESET (0x3C): write-only reset pulse (gsl_yamato_imx.c:342 writes
   0xFFFFFFFF then 0, never reads it); the stateless GPU3D model has no pipeline
   to reset. */
constexpr uint32_t kIdxSoftReset = 0x003Cu;

/* RBBM_CNTL (0x3B): init config, write-only (gsl_yamato_imx.c:347 writes it
   once, never reads it). */
constexpr uint32_t kIdxRbbmCntl = 0x003Bu;

/* MH_ARBITER_CONFIG (0xA40): memory-hub arbiter config, write-only on yamato
   (gsl_yamato_imx.c:350 writes it, never reads); CERF models no GPU memory
   arbitration. */
constexpr uint32_t kIdxMhArbiterConfig = 0x0A40u;

/* SQ_VS_PROGRAM/SQ_PS_PROGRAM (0x21F7/0x21F6): vertex/pixel shader program base,
   write-only (gsl_yamato_imx.c:353-354 clear both to 0 at init, never read);
   render-inert (the CP-ring scan FATALs before any shader-consuming draw). */
constexpr uint32_t kIdxSqVsProgram = 0x21F7u;
constexpr uint32_t kIdxSqPsProgram = 0x21F6u;

/* MH_MMU_CONFIG (0x40): GPU MMU config, write-only accept. The guest reports
   GSL_MMU_TRANSLATION_ENABLED FALSE (kgsl_hal_init) => gpuaddr==physical GPU-wide;
   modeling a translation here would mis-map the CP-ring and render-target
   physical addresses. */
constexpr uint32_t kIdxMhMmuConfig = 0x0040u;

/* MH_INTERRUPT_MASK (0xA42): MH error-interrupt enable (AXI error / MMU page
   fault). Write-only accept - CERF raises neither (faithful DRAM, MMU disabled),
   so the MH interrupt never fires. */
constexpr uint32_t kIdxMhInterruptMask = 0x0A42u;

constexpr uint32_t kIdxRbbmIntCntl = 0x03B4u;  /* RBBM_INT_CNTL, kgsl_yamato.c:34 */
constexpr uint32_t kIdxCpIntCntl   = 0x01F2u;  /* CP_INT_CNTL, kgsl_ringbuffer.c */
constexpr uint32_t kIdxRbWptrBase  = 0x01C8u;  /* RB_WPTR_BASE, kgsl_ringbuffer_start sub_C101D86C */
constexpr uint32_t kIdxRbWptrDelay = 0x01C6u;  /* RB_WPTR_DELAY */
constexpr uint32_t kIdxRbCntl      = 0x01C1u;  /* RB_CNTL, kgsl_ringbuffer_start sub_C101D86C (RMW) */

/* RBBM_STATUS (0x5D0): the idle poll. kgsl_yamato_idle waits for GUI_ACTIVE
   (bit31) clear (gsl_yamato_imx.c:769) / status ==0x110 (kgsl_yamato.c:792);
   CERF's GPU has no async pipeline, so it is always idle. */
constexpr uint32_t kIdxRbbmStatus     = 0x05D0u;
constexpr uint32_t kRbbmStatusIdle    = 0x00000110u;

/* MASTER_INT_SIGNAL (0x3B7): the yamato ISR (sub_C101AF40) reads it and decodes the
   MH/CP/RBBM/SQ sub-block interrupt bits. CERF's GPU3D asserts no interrupt line
   (poll-based completion, RM Table 3-2 GPU3D=IRQ102 idle only) -> 0 = nothing pending. */
constexpr uint32_t kIdxMasterIntSignal = 0x03B7u;

/* SQ_INST_STORE_MANAGMENT (0xD02, a2xx.xml:1237): shader-store partitioning; the draw-context
   save reads it before any write (kgsl_drawctxt.c:1277), so an unwritten read is the power-on
   default (a2xx.xml documents no reset; no shader instruction store modelled -> 0). */
constexpr uint32_t kIdxSqInstStoreManagment = 0x0D02u;

/* SCRATCH_REG2 (0x57A, yamato_reg.h:356): the draw-context fixup IB's scratch register,
   used only to compute the SET_SHADER_BASES operand (kgsl_drawctxt.c:1245-1260). */
constexpr uint32_t kIdxScratchReg2 = 0x057Au;

/* MH_MMU_MPU_BASE/END (0x46/0x47): memory-protection-unit range, write-only.
   The guest DISABLES the MPU (base=0, end=0xFFFFF000 = all pages;
   kgsl_yamato.c:994-999); CERF models no GPU memory protection. */
constexpr uint32_t kIdxMhMmuMpuBase = 0x0046u;
constexpr uint32_t kIdxMhMmuMpuEnd  = 0x0047u;

/* Chip-id read triple PERIPHID1/PERIPHID2/PATCH_RELEASE (0x3F9/0x3FA/0x1):
   getchipid packs these into chip_id, which getproperty copies to devinfo
   non-branching (gsl_yamato_imx.c:556) - nothing gates on it, so 0 is faithful
   (the real PERIPHID values are off-disk). */
constexpr uint32_t kIdxPeriphId1    = 0x03F9u;
constexpr uint32_t kIdxPeriphId2    = 0x03FAu;
constexpr uint32_t kIdxPatchRelease = 0x0001u;

/* CP ring registers (yamato_reg.h; kgsl_ringbuffer_start kgsl_ringbuffer.c:341).
   RB_BASE/RB_RPTR_ADDR feed the CP_RB_WPTR ring-scan; the rest are CP/render
   config that stays inert ONLY because the ring-scan FATALs on any non-ME_INIT
   opcode (no render/timestamp/ucode path runs to consume them). */
constexpr uint32_t kIdxRbEdramInfo  = 0x0F02u;  /* RB_EDRAM_INFO, yamato_reg.h:350 */
constexpr uint32_t kIdxRbBase       = 0x01C0u;  /* REG_CP_RB_BASE, yamato_reg.h:283 */
constexpr uint32_t kIdxRbRptrAddr   = 0x01C3u;  /* REG_CP_RB_RPTR_ADDR, yamato_reg.h:286 */
constexpr uint32_t kIdxRbWptr       = 0x01C5u;  /* REG_CP_RB_WPTR, yamato_reg.h:288 */
constexpr uint32_t kIdxScratchAddr  = 0x01DDu;  /* REG_SCRATCH_ADDR, yamato_reg.h:354 */
constexpr uint32_t kIdxScratchUmsk  = 0x01DCu;  /* REG_SCRATCH_UMSK, yamato_reg.h:357 */
constexpr uint32_t kIdxCpIntAck     = 0x01F4u;  /* REG_CP_INT_ACK, yamato_reg.h:273 */
constexpr uint32_t kIdxCpDebug      = 0x01FCu;  /* REG_CP_DEBUG, yamato_reg.h:268 */
constexpr uint32_t kIdxMeCntl       = 0x01F6u;  /* REG_CP_ME_CNTL, yamato_reg.h:276 */
constexpr uint32_t kIdxMeRamWaddr   = 0x01F8u;  /* REG_CP_ME_RAM_WADDR, yamato_reg.h:278 */
constexpr uint32_t kIdxMeRamData    = 0x01FAu;  /* REG_CP_ME_RAM_DATA, yamato_reg.h:277 */
constexpr uint32_t kIdxPfpUcodeAddr = 0x00C0u;  /* REG_CP_PFP_UCODE_ADDR, yamato_reg.h:280 */
constexpr uint32_t kIdxPfpUcodeData = 0x00C1u;  /* REG_CP_PFP_UCODE_DATA, yamato_reg.h:281 */
constexpr uint32_t kIdxQueueThresh  = 0x01D5u;  /* REG_CP_QUEUE_THRESHOLDS, yamato_reg.h:282 */

/* PM4 packet decode (kgsl_pm4types.h): type=hdr>>30; TYPE3 opcode=(hdr>>8)&0xFF,
   cnt=((hdr>>16)&0x3FFF)+1; TYPE0 writes cnt regs from regindx=hdr&0x7FFF. */
constexpr uint32_t kPm4Type0 = 0u;
constexpr uint32_t kPm4Type2 = 2u;
constexpr uint32_t kPm4Type3 = 3u;
constexpr uint32_t kPm4OpMeInit              = 0x48u;  /* PM4_ME_INIT */
constexpr uint32_t kPm4OpNop                 = 0x10u;  /* PM4_NOP */
constexpr uint32_t kPm4OpIndirectBuffer      = 0x3Fu;  /* PM4_INDIRECT_BUFFER */
constexpr uint32_t kPm4OpIndirectBufferPfd   = 0x37u;  /* PM4_INDIRECT_BUFFER_PFD */
constexpr uint32_t kPm4OpWaitForIdle         = 0x26u;  /* PM4_WAIT_FOR_IDLE */
constexpr uint32_t kPm4OpEventWrite          = 0x46u;  /* PM4_EVENT_WRITE */
constexpr uint32_t kPm4OpInterrupt           = 0x40u;  /* PM4_INTERRUPT */
constexpr uint32_t kPm4OpSetConstant         = 0x2Du;  /* PM4_SET_CONSTANT */
constexpr uint32_t kPm4OpLoadConstantContext = 0x2Eu;  /* PM4_LOAD_CONSTANT_CONTEXT */
constexpr uint32_t kPm4OpRegToMem            = 0x3Eu;  /* PM4_REG_TO_MEM, kgsl_pm4types.h:68 */
constexpr uint32_t kPm4OpImStore             = 0x2Cu;  /* PM4_IM_STORE, kgsl_pm4types.h:148 */
constexpr uint32_t kPm4OpRegRmw              = 0x21u;  /* PM4_REG_RMW, kgsl_pm4types.h:65 */
constexpr uint32_t kPm4OpWaitRegEq           = 0x52u;  /* PM4_WAIT_REG_EQ, kgsl_pm4types.h:53 */
constexpr uint32_t kPm4OpImLoadImmediate     = 0x2Bu;  /* PM4_IM_LOAD_IMMEDIATE, kgsl_pm4types.h:121 */
constexpr uint32_t kPm4OpDrawIndx            = 0x22u;  /* PM4_DRAW_INDX, kgsl_pm4types.h:96 */
constexpr uint32_t kEventCacheFlushTs        = 4u;     /* CACHE_FLUSH_TS, yamato_reg.h:27 */
constexpr uint32_t kEventCacheFlush          = 6u;     /* CACHE_FLUSH, yamato_reg.h:29 */
constexpr uint32_t kRegToMemShadowFlag       = 1u << 30;  /* build_reg_to_mem_range flag, kgsl_drawctxt.c:439 */

/* SET_CONSTANT bank bases: the type field ((tgt>>16)&7) selects a constant bank whose
   values the draw-context save reads back as registers (reg_to_mem). type 4 = register
   0x2000+offset (GSL_HAL_SUBBLOCK_OFFSET, gsl_yamato_imx.c:286 / kgsl_drawctxt.c:360). */
constexpr uint32_t kScBaseAlu   = 0x4000u;  /* type 0: REG_SQ_CONSTANT_0, yamato_reg.h:392 */
constexpr uint32_t kScBaseFetch = 0x4800u;  /* type 1: REG_SQ_FETCH_0, yamato_reg.h:393 */
constexpr uint32_t kScBaseBool  = 0x4900u;  /* type 2: REG_SQ_CF_BOOLEANS, yamato_reg.h:359 */
constexpr uint32_t kScBaseLoop  = 0x4908u;  /* type 3: REG_SQ_CF_LOOP, yamato_reg.h:360 */
constexpr uint32_t kScBaseReg   = 0x2000u;  /* type 4: register file */

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
                case kPm4OpLoadConstantContext: break;  /* loads ALU/TEX from memory the save never reg_to_mem's; render draws FATAL -> inert */
                case kPm4OpImStore: break;  /* copies the (unmodeled) shader instruction store to shadow read back only by IM_LOAD (0x27), which FATALs -> inert */
                case kPm4OpImLoadImmediate: break;  /* loads a shader program (program_shader kgsl_drawctxt.c:400) into the unmodeled shader store, consumed only by a DRAW, which FATALs -> inert */
                case kPm4OpRegRmw: {  /* fixup RMW of SCRATCH_REG2; the operand it computes is read back only by SET_SHADER_BASES (0x4A), which FATALs -> inert */
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
#if CERF_DEV_MODE
                    for (uint32_t d = 0; d < cnt + 1u && d < 8u; ++d)
                        LOG(Caution, "[IBDUMP] 0x%08X[+%u]=0x%08X\n", pa, d, ReadPa32(pa + d * 4u));
#endif
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

        /* dest surface: RB_COLOR_INFO (0x2001) FORMAT[3:0]=COLORX_8_8_8_8, SWAP[10:9]=1 (BGRA),
           BASE[31:12]; RB_SURFACE_INFO (0x2000) pitch[13:0] (a2xx.xml). */
        const uint32_t ci = BlitReg(0x2001u, pa);
        if ((ci & 0xFu) != 5u || ((ci >> 9) & 0x3u) != 1u)
            HaltUnsupportedAccess("blit dest not COLORX_8_8_8_8 SWAP=1", pa, ci);
        const uint32_t dstBase  = ci & 0xFFFFF000u;
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
        if ((sw1 & 0x3Fu) != 6u || (sw0 >> 31) != 0u ||             /* FMT_8_8_8_8, not tiled */
            ((sw3 >> 19) & 0x3u) != 0u || ((sw3 >> 21) & 0x3u) != 0u ||  /* XY_MAG/MIN_FILTER = POINT */
            ((sw3 >> 1) & 0x7u) != 2u || ((sw3 >> 4) & 0x7u) != 1u ||    /* SWIZ_X=Z, SWIZ_Y=Y */
            ((sw3 >> 7) & 0x7u) != 0u || ((sw3 >> 10) & 0x7u) != 3u)     /* SWIZ_Z=X, SWIZ_W=W (BGRA) */
            HaltUnsupportedAccess("blit source not FMT_8_8_8_8 POINT BGRA", pa, sw3);
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

        /* identity 32bpp copy source[0..W,0..H] -> dest[0..W,0..H] (gpuaddr==physical). Validate
           each surface is one contiguously-backed span (start + last byte), then row-copy. */
        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t sSpan = (srcH - 1u) * srcPitch * 4u + srcW * 4u;
        const uint32_t dSpan = (srcH - 1u) * dstPitch * 4u + srcW * 4u;
        const uint8_t* s0 = mem.TryTranslate(srcBase);
        const uint8_t* sN = mem.TryTranslate(srcBase + sSpan - 1u);
        uint8_t*       d0 = mem.TryTranslateWrite(dstBase);
        uint8_t*       dN = mem.TryTranslateWrite(dstBase + dSpan - 1u);
        if (!s0 || !d0 || sN != s0 + (sSpan - 1u) || dN != d0 + (dSpan - 1u))
            HaltUnsupportedAccess("blit surface not contiguously backed", srcBase, dstBase);
        for (uint32_t y = 0u; y < srcH; ++y)
            std::memcpy(d0 + y * dstPitch * 4u, s0 + y * srcPitch * 4u, srcW * 4u);
    }

    /* CP ring kick: scan [rptr_, wptr) as PM4 packets, model the completion, then write
       rptr to the memptrs slot so kgsl_yamato_idle's rptr==wptr poll passes. */
    void HandleRbWptr(uint32_t wptr) {
        if (wptr < rptr_)
            HaltUnsupportedAccess("CP ring wptr wrap", rb_base_, wptr);
        for (uint32_t off = rptr_; off < wptr; ) {
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
        rptr_ = wptr;
        wptr_ = wptr;
        uint8_t* rp = emu_.Get<EmulatedMemory>().TryTranslateWrite(rb_rptr_addr_);
        if (!rp)
            HaltUnsupportedAccess("CP rptr writeback unbacked", rb_rptr_addr_, wptr);
        *reinterpret_cast<uint32_t*>(rp) = wptr;
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
