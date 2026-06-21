#pragma once

#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

namespace cerf_imx51_ssi_detail {

/* i.MX51 SSI (Synchronous Serial Interface) audio port, MCIMX51RM Ch 56 - an
   audio careful-stub (agent_docs/rules.md): the audio driver only config-writes
   the SSI (no status poll), so a passive register file clears its init. */
constexpr uint32_t kSize = 0x00004000u;   /* AIPS slot, RM Table 2-1 */

struct SsiReset { uint32_t off; uint32_t val; };
/* Non-zero reset values, MCIMX51RM Ch 56 register summary (Table, p56-25/26). */
constexpr SsiReset kResets[] = {
    {0x14, 0x00003003u},  /* SISR  (read-only status, FIFOs empty at reset) */
    {0x18, 0x00003003u},  /* SIER  */
    {0x1C, 0x00000200u},  /* STCR  */
    {0x20, 0x00000200u},  /* SRCR  */
    {0x24, 0x00040000u},  /* STCCR */
    {0x28, 0x00040000u},  /* SRCCR */
    {0x2C, 0x00810081u},  /* SFCSR */
    {0x30, 0x00001111u},  /* STR   */
};

template <uint32_t kBase>
class Imx51SsiImpl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetSoc() == SocFamily::iMX51;
    }
    void OnReady() override {
        for (const auto& r : kResets) regs_[r.off >> 2] = r.val;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override { return regs_[(addr - kBase) >> 2]; }
    void WriteWord(uint32_t addr, uint32_t value) override {
        regs_[(addr - kBase) >> 2] = value;
    }

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes(regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes(regs_.data(), sizeof(regs_)); }

private:
    std::array<uint32_t, kSize / 4> regs_{};
};

}  /* namespace cerf_imx51_ssi_detail */
