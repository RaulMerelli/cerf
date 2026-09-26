#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <cstdint>

namespace cerf_imx6_pwm_detail {

/* Linux drivers/pwm/pwm-imx27.c defines the PWM register offsets used by i.MX6. IMX6DQRM Rev.2
   §51.7.1 puts EN at bit 0 of PWMCR and §51.7.4 makes PWMSAR the FIFO input that carries the duty
   cycle; CERF renders no backlight, so accepting them is the whole modelled behaviour. */
template <uint32_t kBase> class Imx6Pwm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }

    uint32_t MmioBase() const override { return kBase; }

private:
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == kOffCr) return control_;
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kOffCr) {
            if ((value & ~kCrEnable) != 0u)
                HaltUnsupportedAccess("imx6-pwm PWMCR clock, prescaler, output and reset control", addr, value);
            control_ = value;
            return;
        }
        if (off == kOffSar) return;
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.Write(control_); }
    void RestoreState(StateReader& r) override { r.Read(control_); }

    static constexpr uint32_t kOffCr = 0x00u;
    static constexpr uint32_t kOffSar = 0x0Cu;
    static constexpr uint32_t kCrEnable = 1u << 0;

    void WriteLane(const Imx6MmioLane& lane) {
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    uint32_t control_ = 0;
};

}

using cerf_imx6_pwm_detail::Imx6Pwm;
