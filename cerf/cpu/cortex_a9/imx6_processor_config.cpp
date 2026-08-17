#include "cortex_a9_processor_config.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

class Imx6ProcessorConfig : public CortexA9ProcessorConfigBase {
public:
    using CortexA9ProcessorConfigBase::CortexA9ProcessorConfigBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    /* Cortex-A9 r2p10 (variant 2, revision 10): the revision shipped on every
       i.MX6 SoC variant per IMX6SDLRM Rev.1 section 8.1
       ("ARM Cortex-A9 MPCore r2p10"). */
    uint32_t Midr() const override { return 0x412FC09Au; }

    /* arm_clk = 800 MHz. Constant 0x2FAF0800 embedded at nk.exe offset 0x187D0
       (the OEMInit clock-tree dump records this as the Cortex-A9 core rate). */
    uint32_t CpuClockHz() const override { return 800000000u; }

    /* GPT/EPIT counter clock = ckih = 24 MHz (IMX6SDLRM section 18.5.1.1.1). */
    uint32_t CpuToOscrDivider()          const override { return 33u; }
    uint32_t CpuToHighfreqClockDivider() const override { return 33u; }

    /* SNVS RTC reference = ckil = 32.768 kHz (IMX6SDLRM section 18.5.2). */
    uint32_t CpuToLowfreqClockDivider()  const override { return 24414u; }

    /* PCB anchor in TPIDRURO; kernel re-sets it at KernelStart. Initial value must
       land in a mapped page; changing to an unmapped VA faults pre-KernelStart. */
    uint32_t InitialTpidruro() const override { return 0xFFFFC7ACu; }

    /* Cortex-A9 with L2C-310 external unified L2 (DDI0388H section 4.3.1, page 4-7:
       LoUU=1 / LoC=2 / LoUIS=1, no Ctype3+, Ctype1=Separate, Ctype2=Unified). */
    uint32_t Clidr() const override { return 0x09000003u; }

    /* IMX6SDLRM section 8.2 + DDI0388H Table 4-2: 32 KB L1, 4-way, 32 B/line.
       Wrong encoding here breaks cp15 cache-maintenance range ops in the JIT. */
    uint32_t Ccsidr(uint32_t csselr) const override {
        const uint32_t level = (csselr >> 1) & 0x7u;
        const uint32_t ind   =  csselr       & 0x1u;
        if (level == 0) {
            return ind ? 0x203FE019u   /* L1 I-cache, 32 KB, 4-way, 32 B */
                       : 0x700FE019u;  /* L1 D-cache, 32 KB, 4-way, 32 B */
        }
        return 0u;
    }
};

/* ARM L2C-310 at the Cortex-A9 private-peripheral base + 0x2000
   (i.MX6SDL RM, ARM MPCore memory map). Cache maintenance completes
   synchronously because CERF has no host-visible guest cache state. */
class Imx6Pl310 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x00A02000u; }
    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t shift = (addr & 3u) * 8u;
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> shift);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t shift = (addr & 2u) * 8u;
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> shift);
    }
    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - MmioBase()) {
        case 0x000: return 0x410000C8u;  /* Cache ID: PL310 */
        case 0x004: return 0x1C100100u;  /* 8-way, 32-byte line */
        case 0x100: return control_;
        case 0x104: return auxiliary_control_;
        case 0x108: return tag_ram_control_;
        case 0x10C: return data_ram_control_;
        case 0x200: return ev_counter_ctrl_;
        case 0x204: return ev_counter1_cfg_;
        case 0x208: return ev_counter0_cfg_;
        case 0x20C: return ev_counter1_;
        case 0x210: return ev_counter0_;
        case 0x214: return int_mask_;
        case 0x218: return int_mask_status_;
        case 0x21C: return int_raw_status_;
        case 0x220: return int_clear_;
        case 0x730: return 0u;  /* Cache Sync */
        case 0x740: return 0u;  /* Dummy register */
        case 0x770: return 0u;  /* Invalidate Line by PA */
        case 0x77C: return 0u;  /* Invalidate by Way */
        case 0x7B0: return 0u;  /* Clean Line by PA */
        case 0x7B8: return 0u;  /* Clean Line by Index/Way */
        case 0x7BC: return 0u;  /* Clean by Way */
        case 0x7F0: return 0u;  /* Clean and Invalidate Line by PA */
        case 0x7F8: return 0u;  /* Clean and Invalidate Line by Index/Way */
        case 0x7FC: return 0u;  /* Clean and Invalidate by Way */
        case 0xF40: return debug_ctrl_;
        case 0xF60: return prefetch_ctrl_;
        case 0xF80: return power_ctrl_;
        default:
            HaltUnsupportedAccess("imx6-pl310 read32 unmodelled register", addr, 0);
        }
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - MmioBase()) {
        case 0x100: control_           = value & 1u;   break;
        case 0x104: auxiliary_control_ = value;        break;
        case 0x108: tag_ram_control_   = value;        break;
        case 0x10C: data_ram_control_  = value;        break;
        case 0x200: ev_counter_ctrl_   = value;        break;
        case 0x204: ev_counter1_cfg_   = value;        break;
        case 0x208: ev_counter0_cfg_   = value;        break;
        case 0x214: int_mask_          = value;        break;
        case 0x21C: int_raw_status_   &= ~value;       break;  /* W1C */
        case 0x220: int_clear_         = value;        break;
        case 0x730: cache_sync_        = value;        break;
        case 0x740: dummy_             = value;        break;
        case 0x770: inv_line_pa_       = value;        break;
        case 0x77C: inv_way_           = value;        break;
        case 0x7B0: clean_line_pa_     = value;        break;
        case 0x7B8: clean_line_idx_    = value;        break;
        case 0x7BC: clean_way_         = value;        break;
        case 0x7F0: clean_inv_line_pa_ = value;        break;
        case 0x7F8: clean_inv_line_idx_= value;        break;
        case 0x7FC: clean_inv_way_     = value;        break;
        case 0xF40: debug_ctrl_        = value;        break;
        case 0xF60: prefetch_ctrl_     = value;        break;
        case 0xF80: power_ctrl_        = value;        break;
        default:
            HaltUnsupportedAccess("imx6-pl310 write32 unmodelled register", addr, value);
        }
    }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned,
            (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t control_          = 0u;
    uint32_t auxiliary_control_= 0x02020000u;
    uint32_t tag_ram_control_  = 0x00000111u;
    uint32_t data_ram_control_ = 0x00000111u;
    uint32_t ev_counter_ctrl_  = 0u;
    uint32_t ev_counter1_cfg_  = 0u;
    uint32_t ev_counter0_cfg_  = 0u;
    uint32_t ev_counter1_      = 0u;
    uint32_t ev_counter0_      = 0u;
    uint32_t int_mask_         = 0u;
    uint32_t int_mask_status_  = 0u;
    uint32_t int_raw_status_   = 0u;
    uint32_t int_clear_        = 0u;
    uint32_t cache_sync_       = 0u;
    uint32_t dummy_            = 0u;
    uint32_t inv_line_pa_      = 0u;
    uint32_t inv_way_          = 0u;
    uint32_t clean_line_pa_    = 0u;
    uint32_t clean_line_idx_   = 0u;
    uint32_t clean_way_        = 0u;
    uint32_t clean_inv_line_pa_= 0u;
    uint32_t clean_inv_line_idx_= 0u;
    uint32_t clean_inv_way_    = 0u;
    uint32_t debug_ctrl_       = 0u;
    uint32_t prefetch_ctrl_    = 0u;
    uint32_t power_ctrl_       = 0u;
};

/* AIPS-TZ bus-bridge access-control registers. OEMInit programs allow bits
   so CE drivers can access SoC peripherals at supervisor level without
   taking an access-violation abort (IMX6SDLRM section 2.6). */
class Imx6Aipstz1 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x0207C000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(
            ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(
            ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0)
            return regs_[off >> 2];
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        MergeWrite(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        MergeWrite(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off < sizeof(regs_) && (off & 3u) == 0) {
            regs_[off >> 2] = value;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

private:
    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned,
            (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
    }

    uint32_t regs_[0x4000u / 4u]{};
};

class Imx6Aipstz2 : public Imx6Aipstz1 {
public:
    using Imx6Aipstz1::Imx6Aipstz1;
    uint32_t MmioBase() const override { return 0x0217C000u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(Imx6ProcessorConfig, ArmProcessorConfig);
REGISTER_SERVICE(Imx6Pl310);
REGISTER_SERVICE(Imx6Aipstz1);
REGISTER_SERVICE(Imx6Aipstz2);
