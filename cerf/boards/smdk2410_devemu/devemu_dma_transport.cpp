#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kBase = 0x500F0000u;
constexpr uint32_t kSpan = 0x2120u;

constexpr uint32_t kSharedSize = 0x2080u;

constexpr uint32_t kHostStatusBase = 0x2000u;
constexpr uint32_t kHostStatusTop  = 0x2010u;

constexpr uint32_t kChannelCount   = 4u;
constexpr uint32_t kChannelStride  = 0x20u;
constexpr uint32_t kChannelBase    = 0x2080u;
constexpr uint32_t kChannelTop     = kChannelBase + kChannelCount * kChannelStride;
constexpr uint32_t kChannelControl = 0x00u;

constexpr uint32_t kChannelProbe = 1u;

class DevEmuDmaTransport : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSpan; }

    uint8_t ReadByte(uint32_t addr) override {
        RequireGuestOwned("ReadByte", addr, sizeof(uint8_t), 0);
        return shared_[addr - kBase];
    }
    uint16_t ReadHalf(uint32_t addr) override {
        RequireGuestOwned("ReadHalf", addr, sizeof(uint16_t), 0);
        uint16_t v = 0;
        std::memcpy(&v, &shared_[addr - kBase], sizeof(v));
        return v;
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off < kSharedSize) {
            RequireGuestOwned("ReadWord", addr, sizeof(uint32_t), 0);
            uint32_t v = 0;
            std::memcpy(&v, &shared_[off], sizeof(v));
            return v;
        }
        if (off >= kChannelBase && off < kChannelTop &&
            (off - kChannelBase) % kChannelStride == kChannelControl) {
            return 0u;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        RequireGuestOwned("WriteByte", addr, sizeof(value), value);
        shared_[addr - kBase] = value;
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        RequireGuestOwned("WriteHalf", addr, sizeof(value), value);
        std::memcpy(&shared_[addr - kBase], &value, sizeof(value));
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        if (off < kSharedSize) {
            RequireGuestOwned("WriteWord", addr, sizeof(value), value);
            std::memcpy(&shared_[off], &value, sizeof(value));
            return;
        }
        if (off >= kChannelBase && off < kChannelTop &&
            (off - kChannelBase) % kChannelStride == kChannelControl &&
            value == kChannelProbe) {
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(shared_.data(), shared_.size());
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(shared_.data(), shared_.size());
    }

private:
    void RequireGuestOwned(const char* op, uint32_t addr,
                           uint32_t size, uint64_t value) const {
        const uint32_t off = addr - kBase;
        if (off + size > kSharedSize)
            HaltUnsupportedAccess(op, addr, value);
        if (off < kHostStatusTop && off + size > kHostStatusBase)
            HaltUnsupportedAccess(op, addr, value);
    }

    std::array<uint8_t, kSharedSize> shared_{};
};

}  /* namespace */

REGISTER_SERVICE(DevEmuDmaTransport);
