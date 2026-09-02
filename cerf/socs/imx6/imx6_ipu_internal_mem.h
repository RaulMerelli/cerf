#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace cerf_imx6_ipu_mem_detail {

constexpr uint32_t kSize = 0x00100000u; /* CPMEM + adjacent IPUv3 internal-memory windows */

template <uint32_t kBase> class Imx6IpuInternalMem : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().RegisterResettable(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        return static_cast<uint8_t>(regs_[off >> 2] >> ((off & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        return static_cast<uint16_t>(regs_[off >> 2] >> ((off & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override { return regs_[(addr - kBase) >> 2]; }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - kBase, sh = (off & 3u) * 8u;
        uint32_t& w = regs_[off >> 2];
        w = (w & ~(0xFFu << sh)) | (static_cast<uint32_t>(value) << sh);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase, sh = (off & 2u) * 8u;
        uint32_t& w = regs_[off >> 2];
        w = (w & ~(0xFFFFu << sh)) | (static_cast<uint32_t>(value) << sh);
    }
    void WriteWord(uint32_t addr, uint32_t value) override { regs_[(addr - kBase) >> 2] = value; }

    void SaveState(StateWriter& w) override { w.WriteBytes(regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_.data(), sizeof(regs_)); }

protected:
    std::array<uint32_t, kSize / 4> regs_{};
};

} /* namespace cerf_imx6_ipu_mem_detail */
