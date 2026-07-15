#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* VR4102 DMAAU (DMA Address Unit), Internal I/O Space 2 (UM Table 11-1): the DMA
   start/current addresses for the AIU-IN / AIU-OUT / FIR channels, consumed by the
   AIU/FIR engines. Per-register masks (UM 11.2) drop addr[31:25] in every High half
   and force bit0=0 in the two AIU base-low halves (FIR base-low is byte-aligned). */
constexpr uint32_t kBase    = 0x0B000020u;
constexpr uint32_t kSize    = 0x20u;
constexpr uint32_t kNumRegs = 12u;   /* 0x0B000020-0x0B000037 */

/* Writable-bit mask per register, indexed (addr-kBase)/2 (UM 11.2.1-11.2.6). */
constexpr uint16_t kMask[kNumRegs] = {
    0xFFFE, 0x01FF,   /* AIUIBALREG, AIUIBAHREG */
    0xFFFF, 0x01FF,   /* AIUIALREG,  AIUIAHREG  */
    0xFFFE, 0x01FF,   /* AIUOBALREG, AIUOBAHREG */
    0xFFFF, 0x01FF,   /* AIUOALREG,  AIUOAHREG  */
    0xFFFF, 0x01FF,   /* FIRBALREG,  FIRBAHREG  */
    0xFFFF, 0x01FF,   /* FIRALREG,   FIRAHREG   */
};

/* Reset value per register: 0xF800 (low halves) / 0x01FF (high halves); the RTCRST and
   Other-resets rows are identical (UM 11.2.1-11.2.6). */
constexpr uint16_t kPowerOn[kNumRegs] = {
    0xF800, 0x01FF, 0xF800, 0x01FF,
    0xF800, 0x01FF, 0xF800, 0x01FF,
    0xF800, 0x01FF, 0xF800, 0x01FF,
};

class Vr4102Dmaau : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4102;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            for (uint32_t i = 0; i < kNumRegs; ++i) reg_[i] = kPowerOn[i];
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        return reg_[RegIndex(addr, "DMAAU ReadHalf")];
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        uint32_t i = RegIndex(addr, "DMAAU WriteHalf");
        reg_[i] = value & kMask[i];
    }
    uint32_t ReadWord(uint32_t addr) override {
        uint32_t lo = PairLow(addr, "DMAAU ReadWord");
        return reg_[lo] | (static_cast<uint32_t>(reg_[lo + 1]) << 16);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        uint32_t lo = PairLow(addr, "DMAAU WriteWord");
        reg_[lo]     = static_cast<uint16_t>(value) & kMask[lo];
        reg_[lo + 1] = static_cast<uint16_t>(value >> 16) & kMask[lo + 1];
    }

    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("DMAAU ReadByte", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t v) override { HaltUnsupportedAccess("DMAAU WriteByte", addr, v); }

    void SaveState(StateWriter& w) override { for (uint16_t r : reg_) w.Write(r); }
    void RestoreState(StateReader& r) override { for (uint16_t& x : reg_) r.Read(x); }

private:
    uint32_t RegIndex(uint32_t addr, const char* what) {
        uint32_t off = addr - kBase;
        if (off >= kNumRegs * 2u || (off & 1u)) HaltUnsupportedAccess(what, addr, 0);
        return off / 2u;
    }
    uint32_t PairLow(uint32_t addr, const char* what) {
        uint32_t lo = RegIndex(addr, what);
        if (lo + 1u >= kNumRegs) HaltUnsupportedAccess(what, addr, 0);
        return lo;
    }

    uint16_t reg_[kNumRegs] = {
        0xF800, 0x01FF, 0xF800, 0x01FF,
        0xF800, 0x01FF, 0xF800, 0x01FF,
        0xF800, 0x01FF, 0xF800, 0x01FF,
    };
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Dmaau);
