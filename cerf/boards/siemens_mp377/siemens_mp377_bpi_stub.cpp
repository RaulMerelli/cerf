#include "../../peripherals/peripheral_base.h"

#include "siemens_mp377_sm501.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_detector.h"

#include <array>
#include <cstdint>

/* Siemens MP 377 Board Peripheral Interface — board-decoded MMIO bands
   declared by the P377 OAL OEMAddressTable (nk.exe @ 0x80409F00):

     VA=0x90000000 PA=0xF0000000  16 MB   FPGA/CPLD board ID + HwInfo
     VA=0x91000000 PA=0xF2000000  32 MB   external bus 1 (touch / PBI)
     VA=0x94000000 PA=0xC0000000 128 MB   ATU primary outbound mem
     VA=0x9C000000 PA=0xD0000000  64 MB   ATU secondary outbound mem

   The PA 0xFF000000 16 MB OAT band overlaps the IOP13xx PMMR
   (0xFFD80000..0xFFE80000) handled by Iop13xxPmmrStub at the SoC layer,
   so it's split into Low (0xFF000000..0xFFD80000) and High
   (0xFFE80000..0xFFFFFFFF wraps — see kFfBandHighEnd below).

   With reads returning 0 the OAL prints "signature not found", "MAC
   signature not found", "I&M Data not found", "No partition table
   found" and falls through to defaults — the tolerated-failure path
   established by the P177 bring-up. Real FPGA/CPLD register semantics
   will land here when surfaced by a later concrete blocker. */
namespace {

static bool Mp377SmiBridgeAliasPa(uint32_t addr) {
    const uint32_t a = addr & ~3u;
    return (a >= 0xC4100028u && a <= 0xC4100034u) ||
           (a >= 0xC4800028u && a <= 0xC4800034u);
}

class Mp377BoardIoStub : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint8_t  ReadByte (uint32_t) override { return 0; }
    uint16_t ReadHalf (uint32_t) override { return 0; }
    uint32_t ReadWord (uint32_t) override { return 0; }
    void     WriteByte(uint32_t, uint8_t)  override {}
    void     WriteHalf(uint32_t, uint16_t) override {}
    void     WriteWord(uint32_t, uint32_t) override {}
};

/* FPGA/CPLD board-ID band. The P377 OAL HWI_Init (nk.exe sub_80446218) reads
   a pointer from the FPGA scratch register at byte offset 0x20, derives a
   physical address pa = (raw & 0x07FFFFFF) - 0x10000000, maps it with
   OALPAtoVA (which ORs the uncached bit, yielding 0xB00xxxxx for the FPGA
   band), copies 0x80 bytes into the kernel HWI struct at 0x81AAF898, and
   validates the EBoot signature bytes [0]=0xA5 / [0x7F]=0x5A. EBoot is not
   run under CERF, so this stub fabricates that handoff: offset 0x20 returns
   0x1000, which resolves back to FPGA byte offset 0x1000, where the HWI block
   is served. The block disarms MRAM (size 0, start gate clear) so the bspio
   GetBootState probe takes its default path instead of dereferencing an
   unmapped MRAM pointer. */
class SiemensMp377BpiStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xF0000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }   /* 16 MB */

    void OnReady() override {
        BuildHwi();
        Mp377BoardIoStub::OnReady();
    }

    uint8_t  ReadByte(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off >= kHwiOff && off < kHwiOff + kHwiSize) return hwi_[off - kHwiOff];
        return 0;
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadByte(addr) | (ReadByte(addr + 1) << 8));
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off == kPtrOff) return kHwiOff;   /* HWI block pointer (see header) */
        return static_cast<uint32_t>(ReadByte(addr)        |
                                     (ReadByte(addr + 1) << 8)  |
                                     (ReadByte(addr + 2) << 16) |
                                     (ReadByte(addr + 3) << 24));
    }

private:
    static constexpr uint32_t kPtrOff  = 0x20u;
    static constexpr uint32_t kHwiOff  = 0x1000u;
    static constexpr uint32_t kHwiSize = 0x80u;

    void BuildHwi() {
        hwi_.fill(0);
        hwi_[0x00] = 0xA5u;             /* EBoot signature, leading  */
        hwi_[0x7F] = 0x5Au;             /* EBoot signature, trailing */
        hwi_[0x0E] = 0x20u;             /* 0x20 << 10 == 0x8000 (OPTypeEx sig) */
        hwi_[0x10] = 0x00u; hwi_[0x11] = 0x80u;  /* MRAM sig = 0x8000 */
        hwi_[0x28] = 0x08u;             /* OPTypeEx = 8 (MP 377 19" Touch) */

        /* MRAM (retentive RAM) window. bspio GetBootState maps MRAM and reads
           [base + 0x7FFDC]; a disarmed window still faulted there because the
           kernel map thunk returns a short mapping. Point MRAM at a backed
           DRAM scratch span instead so the read lands in real memory and
           GetBootState falls through to its default boot state without
           aborting Initialize.EXE (which drives SMIBASE/display init).
             +0x44 gate: (x & 0xF) == 2 arms the start IOCTL
             +0x48 u16 start >> 10  -> 0xE000 << 10 = PA 0x03800000 (56 MB)
             +0x24 BE32 size        -> 0x00100000 (1 MB) */
        hwi_[0x44] = 0x02u;
        hwi_[0x48] = 0x00u; hwi_[0x49] = 0xE0u;       /* 0xE000 -> 56 MB */
        hwi_[0x24] = 0x00u; hwi_[0x25] = 0x10u;
        hwi_[0x26] = 0x00u; hwi_[0x27] = 0x00u;       /* BE32 0x00100000 */
    }

    std::array<uint8_t, kHwiSize> hwi_{};
};

class SiemensMp377Ebus1Stub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xF2000000u; }
    uint32_t MmioSize() const override { return 0x02000000u; }   /* 32 MB */
};

class SiemensMp377AtuOutboundPrimaryStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xC0000000u; }
    uint32_t MmioSize() const override { return 0x08000000u; }   /* 128 MB */

    uint8_t ReadByte(uint32_t a) override {
        if (Mp377SmiBridgeAliasPa(a))
            return static_cast<uint8_t>(siemens_mp377::Mp377SmiBridgeRead(a & ~3u) >> ((a & 3u) * 8u));
        return Mp377BoardIoStub::ReadByte(a);
    }
    uint16_t ReadHalf(uint32_t a) override {
        if (Mp377SmiBridgeAliasPa(a))
            return static_cast<uint16_t>(siemens_mp377::Mp377SmiBridgeRead(a & ~3u) >> ((a & 2u) * 8u));
        return Mp377BoardIoStub::ReadHalf(a);
    }
    uint32_t ReadWord(uint32_t a) override {
        if (Mp377SmiBridgeAliasPa(a))
            return siemens_mp377::Mp377SmiBridgeRead(a);
        return Mp377BoardIoStub::ReadWord(a);
    }
    void WriteByte(uint32_t a, uint8_t v) override {
        if (Mp377SmiBridgeAliasPa(a)) {
            uint32_t w = siemens_mp377::Mp377SmiBridgeRead(a & ~3u);
            const uint32_t shift = (a & 3u) * 8u;
            w = (w & ~(0xFFu << shift)) | (static_cast<uint32_t>(v) << shift);
            siemens_mp377::Mp377SmiBridgeWrite(a & ~3u, w);
            return;
        }
        Mp377BoardIoStub::WriteByte(a, v);
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        if (Mp377SmiBridgeAliasPa(a)) {
            uint32_t w = siemens_mp377::Mp377SmiBridgeRead(a & ~3u);
            const uint32_t shift = (a & 2u) * 8u;
            w = (w & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(v) << shift);
            siemens_mp377::Mp377SmiBridgeWrite(a & ~3u, w);
            return;
        }
        Mp377BoardIoStub::WriteHalf(a, v);
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        if (Mp377SmiBridgeAliasPa(a)) {
            siemens_mp377::Mp377SmiBridgeWrite(a, v);
            return;
        }
        Mp377BoardIoStub::WriteWord(a, v);
    }
};

/* The ATU secondary outbound window is split into two stub bands to flank
   the OneNAND chip at PA 0xD0240000..0xD0280000, which is handled by
   SiemensMp377OneNand. PeripheralDispatcher rejects overlaps. */
class SiemensMp377AtuOutboundSecondaryLowStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xD0000000u; }
    uint32_t MmioSize() const override { return 0x00240000u; }   /* up to chip base */
};
class SiemensMp377AtuOutboundSecondaryHighStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xD0280000u; }   /* past chip end */
    uint32_t MmioSize() const override { return 0x03D80000u; }   /* remainder */
};

/* PA 0xFF000000+16MB band, flanking the IOP13xx PMMR at 0xFFD80000+1MB. */
class SiemensMp377FfBandLowStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xFF000000u; }
    uint32_t MmioSize() const override { return 0x00D80000u; }   /* up to PMMR */
};

class SiemensMp377FfBandHighStub : public Mp377BoardIoStub {
public:
    using Mp377BoardIoStub::Mp377BoardIoStub;
    uint32_t MmioBase() const override { return 0xFFE80000u; }   /* past PMMR */
    uint32_t MmioSize() const override { return 0x00180000u; }   /* to 0xFFFFFFFF */
};

}  /* namespace */

REGISTER_SERVICE(SiemensMp377BpiStub);
REGISTER_SERVICE(SiemensMp377Ebus1Stub);
REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryStub);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryLowStub);
REGISTER_SERVICE(SiemensMp377AtuOutboundSecondaryHighStub);
REGISTER_SERVICE(SiemensMp377FfBandLowStub);
REGISTER_SERVICE(SiemensMp377FfBandHighStub);
