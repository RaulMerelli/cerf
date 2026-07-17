#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace cerf_vr41xx_dmaau_detail {

/* VR41xx DMAAU, Internal I/O Space 2. VR4121 UM Table 12-1 == VR4102 UM Table 11-1 (twelve
   R/W registers 0x0B000020-0x0B000037); detail VR4121 UM 12.2.1-12.2.6, VR4102 UM
   11.2.1-11.2.6. */
constexpr uint32_t kNumRegs = 12u;

/* AIU base-low halves force D0=0 ("Write 0 to this bit", VR4121 UM 12.2.1/12.2.3, VR4102 UM
   11.2.1/11.2.3); AIU address-low + FIR base-low + FIR address-low are R/W on all 16 bits
   (VR4121 UM 12.2.2/12.2.5/12.2.6, VR4102 UM 11.2.2/11.2.5/11.2.6). */
constexpr uint16_t kLowMask[6] = {
    0xFFFEu,   /* AIUIBALREG 0x20 */
    0xFFFFu,   /* AIUIALREG  0x24 */
    0xFFFEu,   /* AIUOBALREG 0x28 */
    0xFFFFu,   /* AIUOALREG  0x2C */
    0xFFFFu,   /* FIRBALREG  0x30 */
    0xFFFFu,   /* FIRALREG   0x34 */
};

/* Reset: Low halves 0xF800, High halves 0x01FF; RTCRST == After-reset row for every register
   (VR4121 UM 12.2.1-12.2.6, VR4102 UM 11.2.1-11.2.6). */
constexpr uint16_t kPowerOn[kNumRegs] = {
    0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
    0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
    0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
};

struct Vr41xxDmaauModel {
    uint32_t base;
    uint32_t size;
    /* High-register writable bits: VR4102 D[8:0]=0x01FF (addr[24:16]); VR4121 D[10:0]=0x07FF
       (addr[26:16]) - D9/D10 R/W on VR4121, RFU-read-0 on VR4102 (VR4121 UM 12.2.1, VR4102
       UM 11.2.1). */
    uint16_t high_writable_mask;
};

template <SocFamily Soc, Vr41xxDmaauModel M>
class Vr41xxDmaauBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == Soc;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            for (uint32_t i = 0; i < kNumRegs; ++i) reg_[i] = kPowerOn[i];
        });
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }

    uint16_t ReadHalf(uint32_t addr) override {
        return reg_[RegIndex(addr, "DMAAU ReadHalf")];
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        uint32_t i = RegIndex(addr, "DMAAU WriteHalf");
        reg_[i] = static_cast<uint16_t>(value & MaskFor(i));
    }
    uint32_t ReadWord(uint32_t addr) override {
        uint32_t lo = PairLow(addr, "DMAAU ReadWord");
        return reg_[lo] | (static_cast<uint32_t>(reg_[lo + 1]) << 16);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        uint32_t lo = PairLow(addr, "DMAAU WriteWord");
        reg_[lo]     = static_cast<uint16_t>(value)       & MaskFor(lo);
        reg_[lo + 1] = static_cast<uint16_t>(value >> 16) & MaskFor(lo + 1);
    }

    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("DMAAU ReadByte", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t v) override { HaltUnsupportedAccess("DMAAU WriteByte", addr, v); }

    void SaveState(StateWriter& w) override { for (uint16_t r : reg_) w.Write(r); }
    void RestoreState(StateReader& r) override { for (uint16_t& x : reg_) r.Read(x); }

private:
    static uint16_t MaskFor(uint32_t i) {
        return (i & 1u) ? M.high_writable_mask : kLowMask[i / 2u];
    }
    uint32_t RegIndex(uint32_t addr, const char* what) {
        uint32_t off = addr - M.base;
        if (off >= kNumRegs * 2u || (off & 1u)) HaltUnsupportedAccess(what, addr, 0);
        return off / 2u;
    }
    uint32_t PairLow(uint32_t addr, const char* what) {
        uint32_t lo = RegIndex(addr, what);
        if (lo + 1u >= kNumRegs) HaltUnsupportedAccess(what, addr, 0);
        return lo;
    }

    uint16_t reg_[kNumRegs] = {
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
        0xF800u, 0x01FFu, 0xF800u, 0x01FFu,
    };
};

}  /* namespace cerf_vr41xx_dmaau_detail */
