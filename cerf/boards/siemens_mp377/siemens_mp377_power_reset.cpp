#include "siemens_mp377_power_reset.h"

#include "../../peripherals/peripheral_base.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <array>
#include <cstdint>

namespace {

class SiemensMp377PowerReset : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        regs_.fill(0);
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return siemens_mp377::kMp377PowerResetBase; }
    uint32_t MmioSize() const override {
        return siemens_mp377::kMp377PowerResetEnd - siemens_mp377::kMp377PowerResetBase;
    }

    uint8_t ReadByte(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 power/reset byte read", addr, 0);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 power/reset halfword read", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override {
        const int idx = KnownRegisterIndex(addr);
        if (idx >= 0) {
            const uint32_t value = regs_[static_cast<size_t>(idx)];
            return value;
        }
        HaltUnsupportedAccess("MP377 power/reset unknown word read", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("MP377 power/reset byte write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("MP377 power/reset halfword write", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const int idx = KnownRegisterIndex(addr);
        if (idx >= 0) {
            regs_[static_cast<size_t>(idx)] = value;
            return;
        }
        HaltUnsupportedAccess("MP377 power/reset unknown word write", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_.data(), regs_.size() * sizeof(regs_[0]));
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_.data(), regs_.size() * sizeof(regs_[0]));
    }

private:
    static int KnownRegisterIndex(uint32_t addr) {
        const uint32_t rel = addr - siemens_mp377::kMp377PowerResetBase;
        const uint32_t block = rel / siemens_mp377::kMp377PowerResetBlockBytes;
        const uint32_t off = rel % siemens_mp377::kMp377PowerResetBlockBytes;

        /* P377 D018 power/reset model, restricted to offsets proven by
           decompiled guest users.

           OALIoCtlHalLaunch uses:
             block 0/1: [0x10]=0, [0x48]&=~0x300, [0x50]=1,
                        [0x04]&=~0xC0, [0x08]|=1
             block 2:   [0x20]|=2

           PowerFail.dll uses the same three D018 blocks after SYSINTR 23:
             block 0: [0x10]=0, [0x48]&=~0x200 then |=0x100,
                      [0x54]=0x200, [0x5C]=0x200
             block 1: [0x10]=0, [0x48]&=~0x200 then |=0x100,
                      [0x54]=0x200, [0x58]=0x200
             block 2: [0x64]&=~0x1000, [0x68]&=~0x1000,
                      [0x70]&=~0x1000, [0x74]&=~0x1000

           This is passive: it stores/returns only the observed D018 registers.
           It does not synthesize a power-fail condition and does not assert
           raw IRQ 0x1F.  Other offsets stay loud. */
        if (block < 2u) {
            const uint32_t base = block * 8u;
            switch (off) {
                case 0x04u: return static_cast<int>(base + 0u);
                case 0x08u: return static_cast<int>(base + 1u);
                case 0x10u: return static_cast<int>(base + 2u);
                case 0x48u: return static_cast<int>(base + 3u);
                case 0x50u: return static_cast<int>(base + 4u);
                case 0x54u: return static_cast<int>(base + 5u);
                case 0x58u: return static_cast<int>(base + 6u);
                case 0x5Cu: return static_cast<int>(base + 7u);
                default: break;
            }
        } else if (block == 2u) {
            switch (off) {
                case 0x20u: return 16;
                case 0x64u: return 17;
                case 0x68u: return 18;
                case 0x70u: return 19;
                case 0x74u: return 20;
                default: break;
            }
        }
        return -1;
    }

    std::array<uint32_t, 21> regs_{};
};

}  // namespace

REGISTER_SERVICE(SiemensMp377PowerReset);

