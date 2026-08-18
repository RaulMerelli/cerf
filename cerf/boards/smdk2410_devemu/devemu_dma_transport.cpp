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

/* devemu_wm2003se dmatrans.dll sub_19111BC 0x19111BC maps the window from
   0xB10F0000 over 0x2120 bytes and publishes each channel's flag dword as
   that base + 0x2000 + 4n. */
constexpr uint32_t kMappedWindowBase = 0xB10F0000u;

constexpr uint32_t kSharedSize = 0x2080u;

constexpr uint32_t kHostStatusBase = 0x2000u;
constexpr uint32_t kHostStatusTop  = 0x2010u;

constexpr uint32_t kChannelCount   = 4u;
constexpr uint32_t kChannelStride  = 0x20u;
constexpr uint32_t kChannelBase    = 0x2080u;
constexpr uint32_t kChannelTop     = kChannelBase + kChannelCount * kChannelStride;
constexpr uint32_t kChannelControl = 0x00u;
constexpr uint32_t kChannelConfig  = 0x04u;
constexpr uint32_t kChannelIrq     = 0x10u;

constexpr uint32_t kChannelPresent = 0x001u;
constexpr uint32_t kChannelEnable  = 0x100u;
constexpr uint32_t kChannelProbe   = 0x001u;

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
        if (off >= kChannelBase && off < kChannelTop) {
            const uint32_t n   = (off - kChannelBase) / kChannelStride;
            const uint32_t reg = (off - kChannelBase) % kChannelStride;
            if (reg == kChannelControl)
                return ch_control_[n] | kChannelPresent;
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
        if (off >= kChannelBase && off < kChannelTop) {
            const uint32_t n   = (off - kChannelBase) / kChannelStride;
            const uint32_t reg = (off - kChannelBase) % kChannelStride;
            if (reg == kChannelControl &&
                (value == kChannelProbe ||
                 value == (kChannelPresent | kChannelEnable))) {
                ch_control_[n] = value & kChannelEnable;
                return;
            }
            if (reg == kChannelConfig &&
                (value == kBase + kHostStatusBase + 4u * n ||
                 value == kMappedWindowBase + kHostStatusBase + 4u * n)) {
                ch_config_[n] = value;
                return;
            }
            if (reg == kChannelIrq) {
                ch_irq_[n] = value;
                return;
            }
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(shared_.data(), shared_.size());
        for (uint32_t v : ch_control_) w.Write<uint32_t>(v);
        for (uint32_t v : ch_config_)  w.Write<uint32_t>(v);
        for (uint32_t v : ch_irq_)     w.Write<uint32_t>(v);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(shared_.data(), shared_.size());
        for (uint32_t& v : ch_control_) r.Read(v);
        for (uint32_t& v : ch_config_)  r.Read(v);
        for (uint32_t& v : ch_irq_)     r.Read(v);
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
    std::array<uint32_t, kChannelCount> ch_control_{};
    std::array<uint32_t, kChannelCount> ch_config_{};
    std::array<uint32_t, kChannelCount> ch_irq_{};
};

}  /* namespace */

REGISTER_SERVICE(DevEmuDmaTransport);
