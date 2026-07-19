#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace cerf_vr4122_reg_window_detail {

constexpr uint32_t kMaxRegs = 13u;

struct Vr4122RegWindowModel {
    uint32_t base;
    uint32_t size;
    uint32_t num_regs;
    uint16_t wmask[kMaxRegs];
    uint16_t reset[kMaxRegs];
    uint16_t fatal_on_set[kMaxRegs];
};

template <Vr4122RegWindowModel M>
class Vr4122RegWindowBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4122;
    }
    void OnReady() override {
        for (uint32_t i = 0; i < M.num_regs; ++i) reg_[i] = M.reset[i];
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            for (uint32_t i = 0; i < M.num_regs; ++i) reg_[i] = M.reset[i];
        });
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }

    uint16_t ReadHalf(uint32_t addr) override {
        return reg_[RegIndex(addr, "ReadHalf")];
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t i = RegIndex(addr, "WriteHalf");
        if (value & M.fatal_on_set[i])
            HaltUnsupportedAccess("WriteHalf sets an unmodeled trigger bit", addr, value);
        reg_[i] = static_cast<uint16_t>((reg_[i] & ~M.wmask[i]) | (value & M.wmask[i]));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t lo = PairLow(addr, "ReadWord");
        return reg_[lo] | (static_cast<uint32_t>(reg_[lo + 1]) << 16);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        PairLow(addr, "WriteWord");
        WriteHalf(addr,     static_cast<uint16_t>(value));
        WriteHalf(addr + 2, static_cast<uint16_t>(value >> 16));
    }

    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("ReadByte", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t v) override { HaltUnsupportedAccess("WriteByte", addr, v); }

    void SaveState(StateWriter& w) override { for (uint32_t i = 0; i < M.num_regs; ++i) w.Write(reg_[i]); }
    void RestoreState(StateReader& r) override { for (uint32_t i = 0; i < M.num_regs; ++i) r.Read(reg_[i]); }

private:
    uint32_t RegIndex(uint32_t addr, const char* what) {
        const uint32_t off = addr - M.base;
        if (off >= M.num_regs * 2u || (off & 1u)) HaltUnsupportedAccess(what, addr, 0);
        return off / 2u;
    }
    uint32_t PairLow(uint32_t addr, const char* what) {
        const uint32_t lo = RegIndex(addr, what);
        if (lo + 1u >= M.num_regs) HaltUnsupportedAccess(what, addr, 0);
        return lo;
    }

    uint16_t reg_[kMaxRegs] = {};
};

}  /* namespace cerf_vr4122_reg_window_detail */
