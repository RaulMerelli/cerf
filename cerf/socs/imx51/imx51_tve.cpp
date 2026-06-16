#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace {

/* i.MX51 TVE (TV Encoder), MCIMX51RM Ch 58, base 0x83FF4000 (Table 2-1).
   Config-register block (Table 58-11); byte/half/word register file. */
constexpr uint32_t kBase = 0x83FF4000u;
constexpr uint32_t kSize = 0x00004000u;

class Imx51Tve : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t a) override {
        const uint32_t o = a - kBase;
        return static_cast<uint8_t>(regs_[o >> 2] >> ((o & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t a) override {
        const uint32_t o = a - kBase;
        return static_cast<uint16_t>(regs_[o >> 2] >> ((o & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t a) override { return regs_[(a - kBase) >> 2]; }

    void WriteByte(uint32_t a, uint8_t  v) override { Merge(a - kBase, v, (a & 3u) * 8u, 0xFFu); }
    void WriteHalf(uint32_t a, uint16_t v) override { Merge(a - kBase, v, (a & 2u) * 8u, 0xFFFFu); }
    void WriteWord(uint32_t a, uint32_t v) override { regs_[(a - kBase) >> 2] = v; }

    void SaveState(StateWriter& w) override    { w.WriteBytes(regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_.data(), sizeof(regs_)); }

private:
    void Merge(uint32_t off, uint32_t v, uint32_t shift, uint32_t vmask) {
        const uint32_t m = vmask << shift;
        uint32_t& r = regs_[off >> 2];
        r = (r & ~m) | ((v << shift) & m);
    }

    std::array<uint32_t, kSize / 4> regs_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Tve);
