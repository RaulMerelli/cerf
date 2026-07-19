#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace cerf_vr41xx_led_detail {

/* NEC VR41xx LED Control Unit (VR4102 UM ch.23 == VR4131 UM ch.19). */
constexpr uint32_t kOffHts  = 0x00u;   /* LEDHTSREG (+ LEDLTSREG 0x02), one 32-bit store */
constexpr uint32_t kOffCnt  = 0x08u;   /* LEDCNTREG */
constexpr uint32_t kOffAstc = 0x0Au;   /* LEDASTCREG */

/* LEDCNTREG: D1 LEDSTOP | D0 LEDENABLE R/W, D15:2 RFU; RTCRST = LEDSTOP(1)/LEDENABLE(0),
   other-resets retained (VR4131 UM 19.2.3, p.361; VR4102 UM 23.2.3). */
constexpr uint16_t kCntMask    = 0x0003u;
constexpr uint16_t kCntPowerOn = 0x0002u;

/* LEDHTS/LTS/ASTC set the external LEDOUT# blink timing (VR4131 UM 19.2.1, p.359); CERF
   renders no LED, so those writes are inert and every read but LEDCNT (the one register the
   driver RMWs) born-FATALs. LEDINTREG (auto-stop interrupt) is born-FATAL - no driver IST. */
template <SocFamily Soc, uint32_t Base>
class Vr41xxLedBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == Soc;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) cnt_ = kCntPowerOn;
        });
    }

    uint32_t MmioBase() const override { return Base; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - Base == kOffCnt) return cnt_;
        HaltUnsupportedAccess("LED ReadHalf", addr, 0);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - Base) {
            case kOffCnt:  SetCnt(value); return;
            case kOffAstc: return;
            default: WriteHalfExt(addr, value); return;
        }
    }
    /* The CE OAL drives LEDCNTREG/LEDASTCREG with byte stores (nk.exe 0xBF033BC0
       "sb -> 0x0F000188"); NetBSD vrled.c:80 uses halfword - the unit takes both. */
    void WriteByte(uint32_t addr, uint8_t value) override {
        switch (addr - Base) {
            case kOffCnt:  SetCnt(value); return;
            case kOffAstc: return;
            default: HaltUnsupportedAccess("LED WriteByte", addr, value);
        }
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - Base == kOffHts) return;
        HaltUnsupportedAccess("LED WriteWord", addr, value);
    }

    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("LED ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("LED ReadWord", addr, 0); }

    void SaveState(StateWriter& w) override { w.Write(cnt_); }
    void RestoreState(StateReader& r) override { r.Read(cnt_); }

protected:
    virtual void WriteHalfExt(uint32_t addr, uint16_t value) {
        HaltUnsupportedAccess("LED WriteHalf", addr, value);
    }

private:
    void SetCnt(uint16_t v) { cnt_ = static_cast<uint16_t>(v & kCntMask); }

    uint16_t cnt_ = kCntPowerOn;
};

}  /* namespace cerf_vr41xx_led_detail */
