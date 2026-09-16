#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#pragma once

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "imx6_mmio_lane.h"

#include <cstdint>

namespace {

/* Linux drivers/pwm/pwm-imx27.c defines PWM offsets, SWR self-clear, FIFOAV,
   and enabled-counter behavior used by i.MX6. */
template <uint32_t kBase> class Imx6Pwm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        regs_[kOffSr >> 2] = kSrFifoAv4Words;
        regs_[kOffPr >> 2] = 0x0000FFFFu;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

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
        switch (off) {
        case kOffCr: return regs_[kOffCr >> 2] & ~kCrSwr;
        case kOffSr: return (regs_[kOffSr >> 2] & ~kSrFifoAvMask) | kSrFifoAv4Words;
        case kOffCnr: return Counter();
        case kOffIr:
        case kOffSar:
        case kOffPr: return regs_[off >> 2];
        default: HaltUnsupportedAccess("read32", addr, 0);
        }
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kOffCr:
            regs_[kOffCr >> 2] = value & ~kCrSwr;
            if (value & kCrSwr) {
                regs_[kOffSr >> 2] = kSrFifoAv4Words;
                regs_[kOffSar >> 2] = 0;
                counter_ = 0;
            }
            return;
        case kOffSr:
            regs_[kOffSr >> 2] &= ~value;
            regs_[kOffSr >> 2] |= kSrFifoAv4Words;
            return;
        case kOffIr:
        case kOffSar:
        case kOffPr:
            regs_[off >> 2] = value;
            if (off == kOffSar) regs_[kOffSr >> 2] = kSrFifoAv4Words;
            return;
        case kOffCnr: counter_ = value; return;
        default: HaltUnsupportedAccess("write32", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes(regs_, sizeof(regs_));
        w.Write(counter_);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes(regs_, sizeof(regs_));
        r.Read(counter_);
    }

private:
    static constexpr uint32_t kOffCr = 0x00u;
    static constexpr uint32_t kOffSr = 0x04u;
    static constexpr uint32_t kOffIr = 0x08u;
    static constexpr uint32_t kOffSar = 0x0Cu;
    static constexpr uint32_t kOffPr = 0x10u;
    static constexpr uint32_t kOffCnr = 0x14u;

    static constexpr uint32_t kCrEn = 1u << 0;
    static constexpr uint32_t kCrSwr = 1u << 3;

    static constexpr uint32_t kSrFifoAvMask = 0x7u;
    static constexpr uint32_t kSrFifoAv4Words = 0x4u;

    uint32_t Counter() {
        if (regs_[kOffCr >> 2] & kCrEn) {
            const uint32_t period = regs_[kOffPr >> 2] ? regs_[kOffPr >> 2] : 1u;
            counter_ = (counter_ + 1u) % (period + 2u);
        }
        return counter_;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == kOffSr) {
            regs_[kOffSr >> 2] &= ~lane.value;
            regs_[kOffSr >> 2] |= kSrFifoAv4Words;
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

    uint32_t regs_[0x18u / 4u]{};
    uint32_t counter_ = 0;
};

}
