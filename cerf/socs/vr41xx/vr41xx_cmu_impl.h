#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace cerf_vr41xx_cmu_detail {

/* Per-chip CMU shape: the VR4102/VR4121 CMUCLKMSK sits at 0x0B000060 (VR4102 UM
   Table 13-1, VR4121 UM Table 14-1) and the VR4122/VR4131 generation moved it to
   0x0F000060 (VR4131 UM Table 10-1) with a different mask-bit set. */
struct Vr41xxCmuModel {
    uint32_t base;
    uint32_t size;
    uint16_t mask;   /* CMUCLKMSK R/W bits; reserved bits "write 0 ... 0 is returned" */
};

/* "The initial value is '0', which specifies masking. No clock is supplied unless the
   CPU writes '1' to CMUCLKMSK register" (VR4102 UM 13.1); both reset rows are 0
   (VR4102 UM 13.2.1, VR4121 UM 14.2.1, VR4131 UM 10.2.1). */
template <SocFamily Soc, Vr41xxCmuModel M>
class Vr41xxCmuBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == Soc;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { clkmsk_ = 0; });
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - M.base == 0) return clkmsk_;
        HaltUnsupportedAccess("VR41xx CMU ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - M.base == 0) { clkmsk_ = value & M.mask; return; }
        HaltUnsupportedAccess("VR41xx CMU WriteHalf", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("VR41xx CMU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("VR41xx CMU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("VR41xx CMU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("VR41xx CMU WriteWord", addr, v); }

    void SaveState(StateWriter& w) override { w.Write(clkmsk_); }
    void RestoreState(StateReader& r) override { r.Read(clkmsk_); }

private:
    uint16_t clkmsk_ = 0;
};

}  /* namespace cerf_vr41xx_cmu_detail */
