#include "../../boards/board_context.h"
#include "../../boards/page_table_builder.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <cstdint>

namespace {

constexpr uint32_t kDdrWindowBase = 0x10000000u;
constexpr uint32_t kGroundedDramEnd = 0x28000000u;
constexpr uint32_t kFirstProbeAddress = 0x3C000000u;
constexpr uint32_t kProbeWord0 = 0x6A08BC95u;
constexpr uint32_t kProbeWord1 = 0xFD1247E3u;
constexpr uint32_t kAbsentProbeWordStub = 0xFFFFFFFFu;

/* IMX6SDLRM Rev.4 §45.4.4.1: access to a non-initialized or disconnected chip select may have unexpected behavior. */
class Imx6MmdcUnpopulatedDram final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        for (const auto& region : emu_.Get<PageTableBuilder>().CachedDramRegions())
            dram_end_ = (std::max)(dram_end_, region.pa_base + region.size);
        if (dram_end_ < kDdrWindowBase)
            emu_.Get<Fatal>().Die("i.MX6 MMDC populated DRAM end 0x%08X is below the DDR aperture", dram_end_);
        size_ = kFirstProbeAddress + 8u - dram_end_;
        probe_addr_ = dram_end_ == kGroundedDramEnd ? kFirstProbeAddress : 0u;
        if (size_ != 0u) emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return dram_end_; }
    uint32_t MmioSize() const override { return size_; }

    uint32_t ReadWord(uint32_t addr) override {
        /* hmi_ktp400_mobile_v13 kernel.dll sub_8032E518 @ VA 0x8032E518: LDRD/STRD/LDRD at
           0x8032E58A/0x8032E592/0x8032E596; caller @ 0x803303E6 drives the 384MiB probe
           from PA 0x3C000000 down to 0x28000000. */
        if (probe_addr_ != 0u) {
            if (phase_ == ProbePhase::InitialWord0 && addr == probe_addr_) {
                phase_ = ProbePhase::InitialWord1;
                return kAbsentProbeWordStub;
            }
            if (phase_ == ProbePhase::InitialWord1 && addr == probe_addr_ + 4u) {
                phase_ = ProbePhase::WriteWord0;
                return kAbsentProbeWordStub;
            }
            if (phase_ == ProbePhase::VerifyWord0 && addr == probe_addr_) {
                phase_ = ProbePhase::VerifyWord1;
                return kAbsentProbeWordStub;
            }
            if (phase_ == ProbePhase::VerifyWord1 && addr == probe_addr_ + 4u) {
                AdvanceProbe();
                return kAbsentProbeWordStub;
            }
        }
        HaltUnsupportedAccess("read32 outside grounded MMDC RAM probe", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (phase_ == ProbePhase::WriteWord0 && addr == probe_addr_ && value == kProbeWord0) {
            phase_ = ProbePhase::WriteWord1;
            return;
        }
        if (phase_ == ProbePhase::WriteWord1 && addr == probe_addr_ + 4u && value == kProbeWord1) {
            phase_ = ProbePhase::VerifyWord0;
            return;
        }
        HaltUnsupportedAccess("write32 outside grounded MMDC RAM probe", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write(probe_addr_);
        w.Write(static_cast<uint8_t>(phase_));
    }

    void RestoreState(StateReader& r) override {
        uint8_t phase = 0;
        r.Read(probe_addr_);
        r.Read(phase);
        phase_ = static_cast<ProbePhase>(phase);
    }

private:
    enum class ProbePhase : uint8_t {
        InitialWord0,
        InitialWord1,
        WriteWord0,
        WriteWord1,
        VerifyWord0,
        VerifyWord1,
    };

    void AdvanceProbe() {
        if (probe_addr_ == dram_end_) {
            probe_addr_ = kFirstProbeAddress;
        } else {
            probe_addr_ = ((dram_end_ >> 1) + (probe_addr_ >> 1)) & ~0xFFFu;
        }
        phase_ = ProbePhase::InitialWord0;
    }

    uint32_t dram_end_ = 0u;
    uint32_t size_ = 0u;
    uint32_t probe_addr_ = 0u;
    ProbePhase phase_ = ProbePhase::InitialWord0;
};

} // namespace

REGISTER_SERVICE(Imx6MmdcUnpopulatedDram);
