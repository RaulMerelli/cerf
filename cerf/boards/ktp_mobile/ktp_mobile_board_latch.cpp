#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* hmi_ktp400_mobile_V14_0_1 keybd.dll +0x27CE maps PA 0x08000000 size 8;
   nleddrvr.dll +0x208A maps the same PA size 4 and +0x1C84 writes halfword +0. */
class KtpMobileBoardLatch : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardContext::IsKtpMobile(bd->GetBoard());
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 8u; }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 1u) != 0u || off >= MmioSize())
            HaltUnsupportedAccess("write-half", addr, value);
        values_[off >> 1] = value;
    }

    void SaveState(StateWriter& w) override { w.WriteBytes(values_, sizeof(values_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(values_, sizeof(values_)); }

private:
    uint16_t values_[4] = {};
};

}

REGISTER_SERVICE(KtpMobileBoardLatch);
