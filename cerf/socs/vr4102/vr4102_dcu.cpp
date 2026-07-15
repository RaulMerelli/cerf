#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* VR4102 DCU (DMA Control Unit), Internal I/O Space 2 (UM Table 12-2): the enable /
   mask / reset / direction control bits for the AIU/FIR DMA sequencer. 16-bit regs;
   the DMA transfer itself is performed by the AIU/FIR units, not the DCU. */
constexpr uint32_t kBase = 0x0B000040u;
constexpr uint32_t kSize = 0x20u;   /* 0x0B000040-0x0B00005F */

constexpr uint32_t kOffRst  = 0x00u;   /* DMARSTREG  0x40: D0 DMARST (0=reset, 1=normal) */
constexpr uint32_t kOffIdle = 0x02u;   /* DMAIDLEREG 0x42: D0 idle status (read-only) */
constexpr uint32_t kOffSen  = 0x04u;   /* DMASENREG  0x44: D0 DMASEN sequencer enable */
constexpr uint32_t kOffMsk  = 0x06u;   /* DMAMSKREG  0x46: D3/D2/D0 per-channel transfer enable */
constexpr uint32_t kOffReq  = 0x08u;   /* DMAREQREG  0x48: D3/D2/D0 request-pending status (read-only) */
constexpr uint32_t kOffTd   = 0x0Au;   /* TDREG      0x4A: D0 FIR transfer direction */

class Vr4102Dcu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        /* DMARSTREG (UM 12.3.1, p282), DMASENREG (12.3.3, p284), DMAMSKREG (12.3.4, p285)
           and TDREG (12.3.6, p287) all have RTCRST and Other-resets rows of 0. */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            rst_ = 0;
            sen_ = 0;
            msk_ = 0;
            td_  = 0;
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffRst: return rst_;
            case kOffSen: return sen_;
            case kOffMsk: return msk_;
            case kOffTd:  return td_;
            case kOffReq: return 0u;   /* AIU/FIR post no DMA request in CERF */
            default: HaltUnsupportedAccess("DCU ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffRst: rst_ = value & 0x0001u; return;
            case kOffSen: sen_ = value & 0x0001u; return;
            case kOffMsk: msk_ = value & 0x000Du; return;
            case kOffTd:  td_  = value & 0x0001u; return;
            /* DMAREQREG is read-only (UM 12.3.5, p286): the request bits are
               driven by the AIU/FIR engines. wavedev RMWs it (&=0xFFF3); the
               silicon ignores the write, so accept it without storing. */
            case kOffReq: return;
            default: HaltUnsupportedAccess("DCU WriteHalf", addr, value);
        }
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("DCU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("DCU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("DCU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("DCU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(rst_); w.Write(sen_); w.Write(msk_); w.Write(td_); }
    void RestoreState(StateReader& r) override { r.Read(rst_); r.Read(sen_); r.Read(msk_); r.Read(td_); }

private:
    uint16_t rst_ = 0;   /* DMARSTREG */
    uint16_t sen_ = 0;   /* DMASENREG */
    uint16_t msk_ = 0;   /* DMAMSKREG */
    uint16_t td_  = 0;   /* TDREG */
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Dcu);
