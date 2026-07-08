#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* VR4102 AIU (Audio Interface Unit), 0x0B000160-0x0B00017F (UM ch.20 Table 20-1).
   CERF models no audio DMA, so the S/M-channel data-FIFO registers stay born-FATAL
   and no AIU interrupt is raised (the wavedev worker then blocks on SYSINTR-13
   without a boot deadlock); only the config registers are modeled. */
constexpr uint32_t kBase = 0x0B000160u;
constexpr uint32_t kSize = 0x20u;

constexpr uint32_t kOffScnt  = 0x08u;   /* SCNTREG  0x168 (UM 20.2.4, p413) */
constexpr uint32_t kOffScnvr = 0x0Au;   /* SCNVRREG 0x16A (UM 20.2.5, p414) */
constexpr uint32_t kOffMcnt  = 0x12u;   /* MCNTREG  0x172 (UM 20.2.7, p416) */
constexpr uint32_t kOffSeq   = 0x1Au;   /* SEQREG   0x17A (UM 20.2.10, p419) */
constexpr uint32_t kOffInt   = 0x1Cu;   /* INTREG   0x17C (UM 20.2.11, p420) */

/* SEQREG: D15 AIURST, D4 AIUMEN, D0 AIUSEN R/W; D14:5 / D3:1 reserved (p419). */
constexpr uint16_t kSeqWritable = 0x8011u;
constexpr uint16_t kSeqAiumen   = 0x0010u;
constexpr uint16_t kSeqAiusen   = 0x0001u;

/* SCNTREG: D15 DAENAIU + D1 SSTOPEN R/W; D3 SSTATE = AIUSEN speaker-run status (R),
   valid only when AIUSEN set; D14:4 / D2 / D0 reserved (p413). */
constexpr uint16_t kScntWritable = 0x8002u;
constexpr uint16_t kScntSstate   = 0x0008u;

/* MCNTREG: D15 ADENAIU + D1 MSTOPEN R/W; D3 MSTATE = AIUMEN (R); D0 ADREQAIU (R,
   0 with no A/D request); D14:4 / D2 reserved (p416). */
constexpr uint16_t kMcntWritable = 0x8002u;
constexpr uint16_t kMcntMstate   = 0x0008u;

/* INTREG: MIC {MENDINTR D11, MINTR D10, MIDLEINTR D9, MSTINTR D8} + SPEAKER
   {SENDINTR D3, SINTR D2, SIDLEINTR D1}, each W1C ("cleared to 0 when 1 is
   written", p420). No source sets them while the unit is disabled. */
constexpr uint16_t kIntCauseMask = 0x0F0Eu;

class Vr4102Aiu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffScnt: return scnt_ | ((seq_ & kSeqAiusen) ? kScntSstate : 0u);
            case kOffMcnt: return mcnt_ | ((seq_ & kSeqAiumen) ? kMcntMstate : 0u);
            case kOffSeq:  return seq_;
            case kOffInt:  return int_;
            default: HaltUnsupportedAccess("AIU ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffScnt: scnt_ = value & kScntWritable; return;
            /* SCNVRREG D/A conversion rate: no CERF audio, accept the write (read
               stays born-FATAL - the guest only sets it). */
            case kOffScnvr: return;
            case kOffMcnt: mcnt_ = value & kMcntWritable; return;
            case kOffSeq:  seq_  = value & kSeqWritable;  return;
            case kOffInt:  int_ &= ~(value & kIntCauseMask); return;
            default: HaltUnsupportedAccess("AIU WriteHalf", addr, value);
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("AIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("AIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("AIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("AIU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(seq_); w.Write(scnt_); w.Write(mcnt_); w.Write(int_); }
    void RestoreState(StateReader& r) override { r.Read(seq_); r.Read(scnt_); r.Read(mcnt_); r.Read(int_); }

private:
    uint16_t seq_  = 0;   /* SEQREG  (AIURST/AIUMEN/AIUSEN) */
    uint16_t scnt_ = 0;   /* SCNTREG (DAENAIU/SSTOPEN)      */
    uint16_t mcnt_ = 0;   /* MCNTREG (ADENAIU/MSTOPEN)      */
    uint16_t int_  = 0;   /* INTREG  (W1C interrupt status) */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Aiu);
