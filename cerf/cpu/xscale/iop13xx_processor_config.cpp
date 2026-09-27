#include "xscale_processor_config_base.h"

#include "../../boards/board_context.h"
#include "../../socs/iop13xx/iop13xx_id.h"
#include "../../core/cerf_emulator.h"

namespace {
class Iop13xxProcessorConfig final : public XscaleProcessorConfigBase {
public:
    using XscaleProcessorConfigBase::XscaleProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Iop13xx;
    }
    /* Linux v3.18 arch/arm/mm/proc-xsc3.S identifies Intel XSC3 with
       MIDR value/mask 0x69056000/0xffffe000. */
    uint32_t Midr() const override { return 0x69056000u; }
    /* Third Generation Intel XScale Microarchitecture Developer's Manual,
       Table 30: 32-KiB, 4-way I/D L1 caches with 32-byte lines. */
    uint32_t Ctr() const override { return 0x0B192192u; }
    /* Linux v3.18 arch/arm/mach-iop13xx/include/mach/time.h decodes the
       IOP13xx CORE_FREQ_800 strap as 800000000 Hz. */
    uint32_t CpuClockHz() const override { return 800000000u; }
    /* Third Generation Intel XScale Microarchitecture Developer's Manual
       316283-002US section 3.2.2.1 Figure 2: bits[23:20] are PA[35:32] and
       bits[8:5] are SBZ; "Supersections always use Domain 0." */
    ArmSupersectionFormat SupersectionFormat() const override {
        return ArmSupersectionFormat::kPa36;
    }
};
} // namespace
REGISTER_SERVICE_AS(Iop13xxProcessorConfig, ArmProcessorConfig);
