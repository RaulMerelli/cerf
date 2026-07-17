#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* VR41xx DCU (DMA Control Unit), Internal I/O Space 2, 0x0B000040-0x0B00005F. VR4121 UM
   Table 13-2 == VR4102 UM Table 12-2; per-register bits + reset rows (all 0) in VR4121 UM
   13.3.1-13.3.6, VR4102 UM 12.3.1-12.3.6. */
constexpr uint32_t kBase = 0x0B000040u;
constexpr uint32_t kSize = 0x20u;   /* 0x0B000040-0x0B00005F */

constexpr uint32_t kOffRst  = 0x00u;   /* DMARSTREG  0x40: D0 DMARST (0=reset, 1=normal) */
constexpr uint32_t kOffIdle = 0x02u;   /* DMAIDLEREG 0x42: D0 DMAISTAT (1=idle, 0=busy), read-only */
constexpr uint32_t kOffSen  = 0x04u;   /* DMASENREG  0x44: D0 DMASEN sequencer enable */
constexpr uint32_t kOffMsk  = 0x06u;   /* DMAMSKREG  0x46: D3 DMAMSKAIN, D2 DMAMSKAOUT, D0 DMAMSKFOUT */
constexpr uint32_t kOffReq  = 0x08u;   /* DMAREQREG  0x48: D3/D2/D0 request-pending status, read-only */
constexpr uint32_t kOffTd   = 0x0Au;   /* TDREG      0x4A: D0 FIR transfer direction */

class Vr41xxDcu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd) return false;
        const SocFamily soc = bd->GetSoc();
        return soc == SocFamily::VR4102 || soc == SocFamily::VR4121;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
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
            /* DMAREQREG's D3/D2/D0 read the AIU/FIR request lines; CERF posts no DMA
               request (VR4121 UM 13.3.5, VR4102 UM 12.3.5 - R for every bit). */
            case kOffReq: return 0u;
            default: HaltUnsupportedAccess("DCU ReadHalf", addr, 0);
        }
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            /* DMARSTREG D0 (VR4121 UM 13.3.1); DMASENREG D0 (13.3.3); DMAMSKREG D3/D2/D0
               = 0x000D (13.3.4); TDREG D0 (13.3.6). */
            case kOffRst: rst_ = value & 0x0001u; return;
            case kOffSen: sen_ = value & 0x0001u; return;
            case kOffMsk: msk_ = value & 0x000Du; return;
            case kOffTd:  td_  = value & 0x0001u; return;
            /* DMAREQREG is read-only - R for every bit (VR4121 UM 13.3.5, VR4102 UM 12.3.5). */
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

REGISTER_SERVICE(Vr41xxDcu);
