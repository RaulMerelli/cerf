#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* NEC VR41xx CMU (Clock Mask Unit): CMUCLKMSK, 0x0B000060, R/W (VR4121 UM Table 14-1,
   VR4102 UM Table 13-1). */
constexpr uint32_t kBase   = 0x0B000060u;
constexpr uint32_t kSize   = 0x20u;
constexpr uint32_t kOffMsk = 0x00u;

/* D10 MSKFFIR, D9 MSKSHSP, D8 MSKSSIU, D5 MSKDSIU, D4 MSKFIR, D3 MSKKIU, D2 MSKAIU,
   D1 MSKSIU, D0 MSKPIU are R/W ("1: Supply / 0: Mask"); D15:11 and D7:6 are reserved,
   "write 0 when writing. 0 is returned after a read" (VR4121 UM 14.2.1, VR4102 UM
   13.2.1). */
constexpr uint16_t kMask = 0x073Fu;

/* "The initial value is '0', which specifies masking. No clock is supplied unless the CPU
   writes '1' to CMUCLKMSK register" (VR4102 UM 13.1); both reset rows are 0 (VR4121 UM
   14.2.1, VR4102 UM 13.2.1). */
constexpr uint16_t kPowerOn = 0x0000u;

class Vr41xxCmu : public Peripheral {
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
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { clkmsk_ = kPowerOn; });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffMsk) return clkmsk_;
        HaltUnsupportedAccess("VR41xx CMU ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase == kOffMsk) { clkmsk_ = value & kMask; return; }
        HaltUnsupportedAccess("VR41xx CMU WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("VR41xx CMU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("VR41xx CMU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("VR41xx CMU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("VR41xx CMU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(clkmsk_); }
    void RestoreState(StateReader& r) override { r.Read(clkmsk_); }

private:
    uint16_t clkmsk_ = kPowerOn;
};

}  /* namespace */

REGISTER_SERVICE(Vr41xxCmu);
