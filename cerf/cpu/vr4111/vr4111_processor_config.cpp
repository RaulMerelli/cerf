#include "../mips_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"

namespace {

/* NEC VR4111 (uPD30111), VR4110 CPU core; "UM" below = the VR4111 User's
   Manual, U13137EJ2V0UM00. */
class Vr4111ProcessorConfig : public MipsProcessorConfig {
public:
    using MipsProcessorConfig::MipsProcessorConfig;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4111;
    }

    /* PRId (CP0 r15) UM Fig 6-18: [31:16] RFU reads 0, Imp[15:8] = "0x0C for the
       VR4111", Rev[7:0] = "CPU core processor revision number". Rev 0x50 from
       Linux arch/mips/include/asm/cpu.h PRID_REV_VR4111. */
    uint32_t     Prid()     const override { return 0x00000C50u; }

    /* "The on-chip TLB is a fully-associative memory that holds 32 entries,
       which provide mapping to odd/even page pairs for one entry" (UM 6.1). */
    uint32_t     TlbSize()  const override { return 32u; }

    /* "The pages can have five different sizes, 1 K, 4 K, 16 K, 64 K, and
       256 K" (UM 6.1). */
    uint32_t     MinPageShift() const override { return 10u; }

    /* PA 0x20000000-0xFFFFFFFF is a "Mirror image of 0x1FFF FFFF to 0x0000 0000"
       (UM Table 6-6). */
    uint32_t     PhysAddrMask() const override { return 0x1FFFFFFFu; }

    /* "Conforms to MIPS I, II, III instruction sets (with the FPU, LL, LLD, SC,
       and SCD instructions left out)" (UM 1.1). */
    MipsIsaLevel IsaLevel() const override { return MipsIsaLevel::kMips3; }

    /* Int0..Int3 -> Cause IP2..IP5, bits 10..13 (UM Fig 10-2). "Int4 never
       occurs in the VR4111" (ibid.); IP7 is the CP0 timer. */
    uint32_t     DeviceIpMask() const override { return 0x00003C00u; }

    /* "The VR4110 CPU core does not support floating-point instructions since
       it has no Floating-Point Unit (FPU)" (UM 5.6). */
    bool HasFpu()     const override { return false; }
    /* "The VR4110 CPU core does not have the LL bit ... does not support
       instructions which manipulate the LL bit (LL, LLD, SC, SCD)" (UM 5.6). */
    bool HasLlsc()    const override { return false; }
    /* CP0 Count (r9) + Compare (r11) present (UM Fig 6-9). */
    bool HasCounter() const override { return true; }
    /* CP0 WatchLo (r18) + WatchHi (r19) present (UM Fig 6-9). */
    bool HasWatch()   const override { return true; }
    /* "Instructions for power modes (HIBERNATE, STANDBY, SUSPEND) are added in
       the VR4110 CPU core" (UM 5.6). */
    bool HasVr41xxPowerModes() const override { return true; }
    /* MIPS16 execution is gated by the MIPS16EN pin at RTC reset (UM 7.3.7);
       this board's strap state is not established from its ROM - JALX stays
       undecoded (loud-fatal). */
    bool HasMips16() const override { return false; }
};

}  // namespace

REGISTER_SERVICE_AS(Vr4111ProcessorConfig, MipsProcessorConfig);
