#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace {

/* i.MX51 USBOH3, MCIMX51RM Ch 60, base 0x73F80000 (Ch 2 Table 2-1). The non-core
   control block (Table 60-2, 0x800+: USB_CTRL/PHY_CTRL_0/1/...) is a plain R/W
   store for the OAL's PHY/clock config; the HS-USB cores (<0x800) carry
   hardware-fixed capability registers and are left unmodelled (halt). */
constexpr uint32_t kBase     = 0x73F80000u;
constexpr uint32_t kSize     = 0x00004000u;   /* AIPS 16 KB slot */
constexpr uint32_t kNonCore  = 0x00000800u;   /* non-core control block start */

class Imx51Usboh3 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off < kNonCore) HaltUnsupportedAccess("ReadWord(core)", addr, 0);
        return regs_[(off - kNonCore) >> 2];
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (off < kNonCore) HaltUnsupportedAccess("WriteWord(core)", addr, value);
        regs_[(off - kNonCore) >> 2] = value;
    }

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes(regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_.data(), sizeof(regs_)); }

private:
    std::array<uint32_t, (kSize - kNonCore) / 4> regs_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Usboh3);
