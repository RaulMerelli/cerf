#include "casio_cassiopeia_em500_companion.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../host/host_window.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstring>

REGISTER_SERVICE(CasioCassiopeiaEm500Companion);

namespace {

/* nk_main_kernel.exe sub_9F0389EC @0x9F0389EC */
constexpr uint32_t kOffCtrl8904 = 0x8904u;

/* nk_main_kernel.exe sub_9F038A58 @0x9F038A60 */
constexpr uint32_t kOffDataA040 = 0xA040u;

/* nk_main_kernel.exe sub_9F038AF0 @0x9F038AF8 */
constexpr uint32_t kOffBusTimingLo = 0x0010u;
constexpr uint32_t kOffBusTimingHi = 0x004Cu;

/* nk_main_kernel.exe sub_9F03C140 @0x9F03C148, sub_9F03C104 @0x9F03C10C */
constexpr uint32_t kOffMboxCmd = 0x8900u;

/* nk_main_kernel.exe sub_9F032B60 @0x9F032C24, sub_9F03C104 @0x9F03C11C,
   sub_9F03C140 @0x9F03C158 */
constexpr uint32_t kOffMboxResp = 0x8910u;

/* nk_main_kernel.exe @0x9F033168/@0x9F0331AC (fold >>16 into the 0x2C2/0x2C1
   commands); wavedev.dll sub_F62520 (& 0x1D | 0x280). */
constexpr uint32_t kOffMboxResp2 = 0x890Cu;

/* bit0: nk_main_kernel.exe sub_9F03BF60 @0x9F03BF88, @0x9F033884 (ROMHDR ulRAMEnd);
   bit1: spimmc.dll sub_F71B5C @0xF71B5C, DSK_Init @0xF71E3A, pcmcia.dll sub_F817B8
   @0xF817B8; phonedb.net "Casio Cassiopeia EM-500" (MMC slot) */
constexpr uint32_t kOffVariantStrap = 0xA0E0u;
constexpr uint32_t kVariantStrap    = 0x3u;

/* nk_main_kernel.exe @0x9F033048/@0x9F0330B0 + @0x9F0335D4 (nibble-0 -> PMUINTREG
   0xAF0000C0 D9/D0 fallback); keybddr.dll sub_FB2D0C @0xFB2DB4 ("AppWakeUP"),
   @0xFB2E70. */
constexpr uint32_t kOffWakeStatus1 = 0xA042u;
constexpr uint32_t kOffWakeStatus2 = 0xA044u;

/* nk_main_kernel.exe @0x9F03308C, ISR loc_9F036C70. */
constexpr uint32_t kOffSocketStatus = 0xA03Cu;

/* nk_main_kernel.exe @0x9F0331FC-0x9F033208 (|1), @0x9F033238-0x9F033244 (&~1),
   @0x9F033248-0x9F033254 (|2), @0x9F033278-0x9F033284 (&~2). */
constexpr uint32_t kOffCtrlA0D4 = 0xA0D4u;

/* nk_main_kernel.exe @0x9F03C188-0x9F03C194 (lw; |1; sh); sub_9F08EE0C
   SYSINTR-19/28/30/31 cases. */
constexpr uint32_t kOffClk8004 = 0x8004u;

/* nk_main_kernel.exe sub_9F03473C @0x9F034824 (lw; andi 1). */
constexpr uint32_t kOffStatus234 = 0x0234u;

/* nk_main_kernel.exe @0x9F032CDC (sw 0), sub_9F03473C @0x9F034834/@0x9F034844,
   @0x9F038994 (sw 0). */
constexpr uint32_t kOffLatch1110 = 0x1110u;

/* pcmcia.dll (MIPS16) per-socket table unk_F88288: RMW @0xF81878 (lw) + @0xF81880
   (sw |0x30); getters @0xF81840-46 (&4), @0xF8185A-62 (&0x30). */
constexpr uint32_t kOffSocketCtrlAC8 = 0x0AC8u;

/* pcmcia.dll (MIPS16) per-socket table unk_F8828C entry+0x38: RMW @0xF81D76 (lw) +
   @0xF81D80 (sw &~0xC); getter @0xF81D54-66 (not; srl 6; &1). */
constexpr uint32_t kOffSocketA038 = 0xA038u;

/* pcmcia.dll (MIPS16) card-detect getter @0xF818AA-0xF818B8 (lw 8; andi 2);
   bit1=1 = empty per consumers @0xF86B42 (nonzero -> power-down + re-poll) and
   @0xF86DEA (nonzero -> abort power-up). */
constexpr uint32_t kOffCardDetectA008 = 0xA008u;
constexpr uint32_t kCardDetectEmpty   = 0x2u;

/* wavedev.dll sub_F616F4 @0xF616FC-0xF61738, sub_F62520 @0xF6252E-0xF625B8,
   loc_F61EAC @0xF61EC8-0xF61F4C, @0xF6184C-0xF618BE, DMA setters
   @0xF61624-0xF6166A; 0x3C4 polls are lw+beqz only. */
constexpr uint32_t kOffCodec880   = 0x0880u;
constexpr uint32_t kOffCodec884   = 0x0884u;
constexpr uint32_t kOffCodec888   = 0x0888u;
constexpr uint32_t kOffCodec890   = 0x0890u;
constexpr uint32_t kOffCodec898   = 0x0898u;
constexpr uint32_t kOffCodec8A0   = 0x08A0u;
constexpr uint32_t kOffCodecCmd   = 0x03C0u;
constexpr uint32_t kOffCodecReady = 0x03C4u;
constexpr uint32_t kOffLatch1118  = 0x1118u;
constexpr uint32_t kOffDmaLo      = 0x08B0u;
constexpr uint32_t kOffDmaHi      = 0x08BCu;
constexpr uint32_t kOffCodec8C4   = 0x08C4u;
constexpr uint32_t kOffCodecIdxL  = 0x08C8u;
constexpr uint32_t kOffCodecIdxR  = 0x08CCu;

/* nk_main_kernel.exe @0x9F035930 (sw 0x10), idle sub_9F038554 @0x9F0388F4
   (lw; andi 1). */
constexpr uint32_t kOffLatch130C  = 0x130Cu;

/* remocon.dll mapper @0xED181C-0xED1820; init RMW @0xED158C. */
constexpr uint32_t kOffAdcCtrl89C = 0x089Cu;

/* socket.dll init @0xF41966-0xF419A0 (sub_F42258 halfword stores; register
   table @0xF421BC-0xF421D6, descriptor 0xAA008000 @0xF421F8). */
constexpr uint32_t kOffStrap8000  = 0x8000u;
constexpr uint32_t kOffCfg8018    = 0x8018u;

/* Modem-socket accessory-ID pins, read-only (zero ROM writers). socket.dll
   @0xF422C0 (lhu; mask 0x1F @0xF416A0), index-0 code cmpi 1 @0xF41594;
   nk_main_kernel.exe sub_9F03C278/2A4/2D4 (&0xF ==15/==8/==12), suspend gate
   @0x9F038180. 0x1F = connector empty. */
constexpr uint32_t kOffSocketId8010 = 0x8010u;
constexpr uint32_t kSocketIdEmpty   = 0x1Fu;

/* nk_main_kernel.exe sub_9F08EE0C @0x9F08EE28 (lhu) + @0x9F08EE34 (sh &0xFDFF). */
constexpr uint32_t kOffIntCfg8404 = 0x8404u;

/* nk_main_kernel.exe @0x9F0332F0 (sw 2 after the PMUINTREG D4 ack),
   @0x9F0335A0-0x9F0335B4 / @0x9F03371C-0x9F033740 (lw; sw 2; bit0 -> RAM
   0xA0002520). */
constexpr uint32_t kOffCause8800 = 0x8800u;

constexpr uint32_t kOffSibRegsLo = 0x0900u;
constexpr uint32_t kOffSibRegsHi = 0x0910u;

/* nk_main_kernel.exe sub_9F034498 @0x9F0344AC-0x9F0344E4 (lw; ==0x77 guard;
   sw 1/0x11/0x71/0x77), power-down @0x9F038B5C (lw; andi 0x79; sw). */
constexpr uint32_t kOffPanelState904 = 0x0904u;

constexpr uint32_t kOffEdgeCfgLo = 0xA060u;
constexpr uint32_t kOffEdgeCfgHi = 0xA07Eu;

constexpr uint32_t kOffPanelEnable = 0x0980u;
constexpr uint32_t kOffPanel0984   = 0x0984u;
constexpr uint32_t kOffPanel0988   = 0x0988u;
constexpr uint32_t kOffBrightness  = 0x098Cu;
constexpr uint32_t kOffPanel0994   = 0x0994u;
constexpr uint32_t kOffContrast    = 0x099Cu;

constexpr uint32_t kOffBlitGo  = 0x0A00u;
constexpr uint32_t kOffBlitOp  = 0x0A04u;
constexpr uint32_t kOffBlitLen = 0x0A08u;
constexpr uint32_t kOffBlitSrc = 0x0A10u;
constexpr uint32_t kOffBlitDst = 0x0A14u;

/* ddi.dll sub_FC4E38 @0xFC4F30 (v10[1] = 129). */
constexpr uint32_t kBlitOpCopy = 0x81u;

constexpr uint32_t kPaMask = 0x1FFFFFFFu;   /* VR4131 UM Fig 3-1 kseg fold */

}  /* namespace */

bool CasioCassiopeiaEm500Companion::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::CasioCassiopeiaEm500;
}

void CasioCassiopeiaEm500Companion::OnReady() {
    fb_.assign(kFbSize, 0u);
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t CasioCassiopeiaEm500Companion::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) return fb_[off - kFbOffset];
    HaltUnsupportedAccess("EM-500 companion ReadByte", addr, 0);
}

uint16_t CasioCassiopeiaEm500Companion::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) {
        uint16_t v;
        std::memcpy(&v, &fb_[off - kFbOffset], sizeof(v));
        return v;
    }
    if (off == kOffWakeStatus1 || off == kOffWakeStatus2) return 0u;
    if (off == kOffIntCfg8404) return intcfg8404_;
    /* socket.dll sub_F42258 @0xF42284 (lhu) / @0xF42292 (sh), mask 0x1010
       call @0xF41986-0xF41990, table[5]=+4 @0xF421D6. */
    if (off == kOffClk8004) return static_cast<uint16_t>(clk8004_);
    if (off == kOffSocketId8010) return kSocketIdEmpty;
    /* nk_main_kernel.exe @0x9F038904/@0x9F03890C/@0x9F038914 (lh x3, $t1
       overwritten untested, before cop0 0x22); xref-complete, no writer. */
    if (off == 0x0404u) return 0u;
    if (off == kOffCodec880) return static_cast<uint16_t>(codec880_);
    if (off == kOffCodec884) return static_cast<uint16_t>(codec884_);
    if (off == kOffCodec888) return static_cast<uint16_t>(codec888_);
    if (off == kOffCodec8A0) return static_cast<uint16_t>(codec8A0_);
    HaltUnsupportedAccess("EM-500 companion ReadHalf", addr, 0);
}

uint32_t CasioCassiopeiaEm500Companion::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) {
        uint32_t v;
        std::memcpy(&v, &fb_[off - kFbOffset], sizeof(v));
        return v;
    }
    if (off == kOffCtrl8904) return ctrl8904_;
    if (off == kOffMboxCmd) return 0u;
    if (off == kOffMboxResp) return 0u;
    if (off == kOffMboxResp2) return 0u;
    if (off == kOffVariantStrap) return kVariantStrap;
    if (off == kOffSocketStatus) return 0u;
    if (off == kOffStatus234) return 0u;
    if (off == kOffLatch1110) return reg_1110_;
    if (off == kOffSocketCtrlAC8) return socket_ctrl_ac8_;
    if (off == kOffSocketA038) return socket_a038_;
    if (off == kOffCardDetectA008) return kCardDetectEmpty;
    if (off == kOffCodec880) return codec880_;
    if (off == kOffCodec884) return codec884_;
    if (off == kOffCodec888) return codec888_;
    if (off == kOffCodec890) return codec890_;
    if (off == kOffCodec898) return codec898_;
    if (off == kOffCodec8A0) return codec8A0_;
    if (off == kOffLatch1118) return latch1118_;
    if (off == kOffLatch130C) return latch130C_;
    if (off == kOffAdcCtrl89C) return adc_ctrl_89C_;
    /* wavedev.dll 0x3C4 ready polls (lw+beqz, e.g. @0xF6255E-0xF62578). */
    if (off == kOffCodecReady) return 1u;
    if (off == kOffCtrlA0D4) return ctrl_a0d4_;
    if (off == kOffClk8004) return clk8004_;
    if (off == kOffCause8800) return 0u;
    if (off == kOffPanelState904) return sib_regs_[1];
    /* nk_main_kernel.exe @0x9F03C1C4 (lw 0x1C; &~8 after the 0x30 write @0x9F03C1C0). */
    if (off == 0x001Cu) return bus_timing_[(0x001Cu - kOffBusTimingLo) / 4u];
    /* ddi.dll sub_FC4E38 @0xFC4F44: busy poll until 0; the copy completes at GO. */
    if (off == kOffBlitGo) return 0u;
    HaltUnsupportedAccess("EM-500 companion ReadWord", addr, 0);
}

void CasioCassiopeiaEm500Companion::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) { fb_[off - kFbOffset] = value; return; }
    HaltUnsupportedAccess("EM-500 companion WriteByte", addr, value);
}

void CasioCassiopeiaEm500Companion::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) {
        std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value));
        return;
    }
    if (off == kOffDataA040) { data_a040_ = value; return; }
    /* nk_main_kernel.exe sub_9F032B60 @0x9F032C44 */
    if (off == kOffCtrl8904) { ctrl8904_ = value; return; }
    /* nk_main_kernel.exe @0x9F03C194 (sh of the |1 write-back). */
    if (off == kOffClk8004) { clk8004_ = value; return; }
    if (off == kOffIntCfg8404) { intcfg8404_ = value; return; }
    if (off == kOffStrap8000) { strap8000_ = value; return; }
    if (off == kOffCfg8018) return;
    if (off == kOffCodec880) { codec880_ = value; return; }
    if (off == kOffCodec884) { codec884_ = value; return; }
    if (off == kOffCodec888) { codec888_ = value; return; }
    if (off == kOffCodec8A0) { codec8A0_ = value; return; }
    if (off == kOffCodecIdxL || off == kOffCodecIdxR) return;
    if (off >= kOffEdgeCfgLo && off <= kOffEdgeCfgHi && (off & 1u) == 0u) {
        edge_cfg_[(off - kOffEdgeCfgLo) / 2u] = value;
        return;
    }
    HaltUnsupportedAccess("EM-500 companion WriteHalf", addr, value);
}

void CasioCassiopeiaEm500Companion::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
    if (InFb(off)) {
        std::memcpy(&fb_[off - kFbOffset], &value, sizeof(value));
        return;
    }
    WriteReg(off, value);
}

void CasioCassiopeiaEm500Companion::WriteReg(uint32_t off, uint32_t value) {
    switch (off) {
        case kOffCtrl8904: ctrl8904_ = value; return;
        case kOffLatch1110: reg_1110_ = value; return;
        case kOffSocketCtrlAC8: socket_ctrl_ac8_ = value; return;
        case kOffSocketA038: socket_a038_ = value; return;
        case kOffCodec880: codec880_ = value; return;
        case kOffCodec884: codec884_ = value; return;
        case kOffCodec888: codec888_ = value; return;
        case kOffCodec890: codec890_ = value; return;
        case kOffCodec898: codec898_ = value; return;
        case kOffCodec8A0: codec8A0_ = value; return;
        case kOffLatch1118: latch1118_ = value; return;
        case kOffLatch130C: latch130C_ = value; return;
        case kOffAdcCtrl89C: adc_ctrl_89C_ = value; return;
        case kOffStrap8000: strap8000_ = value; return;
        case kOffCfg8018: return;
        case kOffCodecCmd: return;
        case kOffCodec8C4: return;
        case kOffCodecIdxL: return;
        case kOffCodecIdxR: return;
        /* nk_main_kernel.exe @0x9F033174/@0x9F0331B8 (sw of the folded 0x2C2/0x2C1),
           sub_9F03C104 @0x9F03C120, sub_9F03C140 @0x9F03C160 (0x301/0x300). */
        case kOffMboxCmd: mbox_cmd_ = value; return;
        case kOffCtrlA0D4: ctrl_a0d4_ = value; return;
        case kOffCause8800: return;
        case 0x0900u: case 0x0904u: case 0x0908u: case 0x090Cu: case 0x0910u:
            sib_regs_[(off - kOffSibRegsLo) / 4u] = value;
            return;
        /* nk_main_kernel.exe sub_9F034498 @0x9F0344EC (sw 0). */
        case 0x0998u: return;
        case kOffPanelEnable:
            reg_0980_ = value;
            MaybePublishDisplaySize();
            return;
        case kOffPanel0984:  reg_0984_ = value; return;
        case kOffPanel0988:  reg_0988_ = value; return;
        case kOffBrightness: reg_098C_ = value; return;
        case kOffPanel0994:  reg_0994_ = value; return;
        case kOffContrast:   reg_099C_ = value; return;
        case kOffBlitOp:  blit_op_        = value; return;
        case kOffBlitLen: blit_len_words_ = value; return;
        case kOffBlitSrc: blit_src_       = value; return;
        case kOffBlitDst: blit_dst_       = value; return;
        case kOffBlitGo:
            /* ddi.dll sub_FC4E38 @0xFC4F38: only 1 is written to GO. */
            if (value != 1u)
                HaltUnsupportedAccess("EM-500 companion blit GO", kBase + off, value);
            RunBlit();
            return;
        default: break;
    }
    /* nk_main_kernel.exe sub_9F032B60: 0xA0C4/0xA0CC @0x9F032C84, 0xA0DC @0x9F032CEC,
       0x1100-0x1130 @0x9F032C98, 0x0008 @0x9F032D24, 0x03C8 @0x9F032D30; 0x1300
       @0x9F033828/@0x9F033854/@0x9F038970, 0x1338 @0x9F03383C/@0x9F03384C;
       0x0304 sw 0 @0x9F033A84 (sub_9F033A5C). */
    if (off == 0x0008u || off == 0x03C8u ||
        off == 0xA0C4u || off == 0xA0CCu || off == 0xA0DCu ||
        off == 0x0304u || off == 0x1300u || off == 0x1338u ||
        (off >= 0x1100u && off <= 0x1130u && (off & 3u) == 0u)) {
        return;
    }
    if (off >= kOffDmaLo && off <= kOffDmaHi && (off & 3u) == 0u) return;
    if (off >= kOffBusTimingLo && off <= kOffBusTimingHi && (off & 3u) == 0u) {
        bus_timing_[(off - kOffBusTimingLo) / 4u] = value;
        return;
    }
    HaltUnsupportedAccess("EM-500 companion WriteWord", kBase + off, value);
}

void CasioCassiopeiaEm500Companion::RunBlit() {
    if (blit_op_ != kBlitOpCopy)
        HaltUnsupportedAccess("EM-500 companion blit opcode", kBase + kOffBlitOp, blit_op_);
    const uint32_t bytes  = blit_len_words_ * 4u;
    if (bytes == 0u) return;
    const uint32_t src_pa = blit_src_ & kPaMask;
    if (static_cast<uint64_t>(blit_dst_) + bytes > kFbSize) {
        LOG(Caution, "EM-500 companion blit dst=0x%X len=%u exceeds framebuffer\n",
            blit_dst_, bytes);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint8_t* host_src = emu_.Get<EmulatedMemory>().TryTranslate(src_pa);
    if (!host_src) {
        LOG(Caution, "EM-500 companion blit src_pa=0x%X unbacked\n", src_pa);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::memcpy(fb_.data() + blit_dst_, host_src, bytes);
}

void CasioCassiopeiaEm500Companion::MaybePublishDisplaySize() {
    if (size_published_ || !IsDisplayEnabled()) return;
    size_published_ = true;
    emu_.Get<HostWindow>().OnLcdEnabled(GuestW(), GuestH());
}

void CasioCassiopeiaEm500Companion::SaveState(StateWriter& w) {
    w.Write<uint64_t>(fb_.size());
    if (!fb_.empty()) w.WriteBytes(fb_.data(), fb_.size());
    w.Write(mbox_cmd_);
    w.Write(ctrl8904_);
    w.Write(ctrl_a0d4_);
    w.Write(clk8004_);
    w.Write(data_a040_);
    for (uint32_t v : sib_regs_) w.Write(v);
    for (uint32_t v : bus_timing_) w.Write(v);
    for (uint16_t v : edge_cfg_) w.Write(v);
    w.Write(reg_0980_); w.Write(reg_0984_); w.Write(reg_0988_);
    w.Write(reg_098C_); w.Write(reg_0994_); w.Write(reg_099C_);
    w.Write(blit_op_); w.Write(blit_len_words_);
    w.Write(blit_src_); w.Write(blit_dst_);
    w.Write(reg_1110_);
    w.Write(socket_ctrl_ac8_);
    w.Write(socket_a038_);
    w.Write(intcfg8404_);
    w.Write(codec880_);
    w.Write(codec884_);
    w.Write(codec888_);
    w.Write(codec890_);
    w.Write(codec898_);
    w.Write(codec8A0_);
    w.Write(latch1118_);
    w.Write(latch130C_);
    w.Write(strap8000_);
    w.Write(adc_ctrl_89C_);
    w.Write<uint8_t>(size_published_ ? 1u : 0u);
}

void CasioCassiopeiaEm500Companion::RestoreState(StateReader& r) {
    uint64_t n = 0;
    r.Read(n);
    fb_.assign(static_cast<size_t>(n), 0u);
    if (n) r.ReadBytes(fb_.data(), static_cast<size_t>(n));
    r.Read(mbox_cmd_);
    r.Read(ctrl8904_);
    r.Read(ctrl_a0d4_);
    r.Read(clk8004_);
    r.Read(data_a040_);
    for (uint32_t& v : sib_regs_) r.Read(v);
    for (uint32_t& v : bus_timing_) r.Read(v);
    for (uint16_t& v : edge_cfg_) r.Read(v);
    r.Read(reg_0980_); r.Read(reg_0984_); r.Read(reg_0988_);
    r.Read(reg_098C_); r.Read(reg_0994_); r.Read(reg_099C_);
    r.Read(blit_op_); r.Read(blit_len_words_);
    r.Read(blit_src_); r.Read(blit_dst_);
    r.Read(reg_1110_);
    r.Read(socket_ctrl_ac8_);
    r.Read(socket_a038_);
    r.Read(intcfg8404_);
    r.Read(codec880_);
    r.Read(codec884_);
    r.Read(codec888_);
    r.Read(codec890_);
    r.Read(codec898_);
    r.Read(codec8A0_);
    r.Read(latch1118_);
    r.Read(latch130C_);
    r.Read(strap8000_);
    r.Read(adc_ctrl_89C_);
    uint8_t pub = 0;
    r.Read(pub);
    size_published_ = pub != 0;
}
