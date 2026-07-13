#include "../../peripherals/uart16550/uart16550.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/serial/serial_cradle.h"
#include "../../state/state_stream.h"
#include "vr4102_giu.h"
#include "../vr41xx_icu.h"
#include "vr4102_serial_wiring.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace {

/* VR4102 on-chip SIU: NS16550-compatible UART at Internal I/O Space 1 base 0x0C000000,
   byte-addressed (SCR at 0x0C000007); interrupt is SYSINT1 SIUINTR D9 (UM 14.2.1, p295).
   SIUIE reserves D[7..4] (UM 24.2.4, p464) and SIUMC's OUT1/OUT2 are internal and valid
   only under loopback (UM 24.2.9, p473), so neither gates the interrupt. */
class Vr4102Siu : public Uart16550 {
public:
    explicit Vr4102Siu(CerfEmulator& emu)
        : Uart16550(emu, Config{/*ier_mask=*/0x0Fu,
                                /*irq_gate_mcr=*/0u,
                                /*irq_gate_ier=*/0u}) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }

    uint32_t MmioBase() const override { return 0x0C000000u; }
    uint32_t MmioSize() const override { return 0x20u; }   /* SIU block; HSP follows at +0x20 */

    void OnReady() override {
        Uart16550::OnReady();

        auto* wiring = emu_.TryGet<Vr4102SerialWiring>();
        if (!wiring) return;
        modem_ = wiring->ForSiu();
        if (!modem_) return;

        /* GIU pins reset low and carrier is asserted low, so an undriven DCD pin reads as
           carrier already present. */
        SetModemInputs(false, false, false, false);

        cradle_ = std::make_unique<SerialCradle>(emu_, *this, modem_->label);
        SetActivityWidget(cradle_.get());
        emu_.Get<HostWidgetRegistry>().Register(cradle_.get());
        LOG(Periph, "[UART] SIU attached as serial endpoint line '%ls'\n",
            modem_->label.c_str());
    }

    void OnShutdown() override {
        if (cradle_) cradle_->OnShutdown();
    }

    /* A board's carrier pin is asserted low, so the level driven onto the GIU is the
       inverse of the endpoint's DCD. */
    void SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) override {
        if (modem_ && modem_->dcd_giu_pin >= 0) {
            emu_.Get<Vr4102Giu>().SetPinLevel(modem_->dcd_giu_pin, !dcd);
            Uart16550::SetModemInputs(cts, dsr, ri, false);
            return;
        }
        Uart16550::SetModemInputs(cts, dsr, ri, dcd);
    }

protected:
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "SIU"; }

    void SetInterruptLine(bool pending) override {
        emu_.Get<Vr41xxIcu>().SetSysint1Source(kSiuIntr, pending);
    }

    /* 0x08 SIUIRSEL D5:0 R/W (UM 24.2.13, p478). 0x09 is undocumented (UM ch.24
       ends at 0x08) - grounded from serial.dll's baud setup (sub_158116C), which
       pulses bit 0 as a divisor-reload strobe. */
    uint32_t ReadExtReg(uint32_t idx) override {
        if (idx == 8u) return irsel_;
        if (idx == 9u) return baud_reload_;
        return Uart16550::ReadExtReg(idx);
    }
    void WriteExtReg(uint32_t idx, uint32_t value) override {
        if (idx == 8u) { irsel_ = static_cast<uint8_t>(value & 0x3Fu); return; }
        if (idx == 9u) { baud_reload_ = static_cast<uint8_t>(value & 0xFFu); return; }
        Uart16550::WriteExtReg(idx, value);
    }

    void SaveState(StateWriter& w) override {
        Uart16550::SaveState(w); w.Write(irsel_); w.Write(baud_reload_);
        if (cradle_) cradle_->SaveCradleState(w);
    }
    void RestoreState(StateReader& r) override {
        Uart16550::RestoreState(r); r.Read(irsel_); r.Read(baud_reload_);
        if (cradle_) cradle_->RestoreCradleState(r);
    }
    void PostRestore() override {
        Uart16550::PostRestore();
        if (cradle_) cradle_->PostRestore();
    }

private:
    static constexpr uint16_t kSiuIntr = 1u << 9;   /* SYSINT1 D9 SIUINTR (UM 14.2.1, p295) */
    uint8_t irsel_       = 0;   /* SIU 0x08 SIUIRSEL */
    uint8_t baud_reload_ = 0;   /* SIU 0x09 divisor-reload strobe */

    std::optional<Vr4102SerialModem> modem_;
    std::unique_ptr<SerialCradle>    cradle_;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Siu);
