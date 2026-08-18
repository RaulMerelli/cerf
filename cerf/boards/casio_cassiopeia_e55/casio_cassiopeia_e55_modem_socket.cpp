#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/guest_cpu_reset.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <cstdint>

namespace {

/* casio_cassiopeia_e55 serial.dll sub_14A2228 @0x14A2370/@0x14A2380. */
constexpr uint32_t kBase = 0x14008000u;
constexpr uint32_t kSize = 0x1000u;

constexpr uint32_t kOffIntr    = 0x002u;
constexpr uint16_t kEnableMask = 0x0700u;
constexpr uint16_t kReqOalWrite = 0x0001u;

class CasioCassiopeiaE55ModemSocket : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::CasioCassiopeiaE55;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            enable_ = 0u;
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr - kBase == kOffIntr) {
            return enable_;
        }
        return Peripheral::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase == kOffIntr) {
            if (value & ~static_cast<uint16_t>(kEnableMask | kReqOalWrite)) {
                HaltUnsupportedAccess("WriteHalf", addr, value);
            }
            enable_ = static_cast<uint16_t>(value & kEnableMask);
            return;
        }
        Peripheral::WriteHalf(addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(enable_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(enable_);
    }

private:
    uint16_t enable_ = 0u;
};

}  /* namespace */

REGISTER_SERVICE(CasioCassiopeiaE55ModemSocket);
