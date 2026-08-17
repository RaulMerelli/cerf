#include "../page_table_builder.h"

#include "../../boot/board_boot_placer.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../net/network_backend.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../peripherals/sd_card/ktp400_hardware_info.h"
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

/* KTP400 mapping data decoded from the OAL OEMAddressTable in nk.exe.

   IDA evidence used here is the OAL table itself, not a guessed extension:
     OEMAddressTable VA 0x803012FC / PA 0x103012FC
     entry layout: { dwVA, dwPA, dwSize, dwFlags }, 16 bytes per entry
     terminator/header magic at VA 0x8030133C = 0x87654321

   The first three MMIO aliases below are the OAL table entries.  The cached DDR
   row is kept separately for CERF ROM placement and host-side VA->PA service;
   it is not written into the OAL peripheral table.  There is deliberately no
   0x60000000 OAT entry here: that was a CERF invention, not present in the OAL
   table. */
constexpr OatEntry kOat[] = {
    /* CERF placement window for cached DDR.  Not an OAL peripheral OAT entry. */
    { 0x80000000u, 0x10000000u, MB(384), OatKind::Dram },

    /* OAL OEMAddressTable[0] @ VA 0x803012FC: flags 0x00000C00. */
    { 0x98000000u, 0x00100000u, MB(3),   OatKind::Mmio },

    /* OAL OEMAddressTable[1] @ VA 0x8030130C: flags 0x00000C00. */
    { 0x98300000u, 0x000C0000u, MB(33),  OatKind::Mmio },

    /* OAL OEMAddressTable[2] @ VA 0x8030131C: flags 0x00000C00. */
    { 0x9A400000u, 0x00000000u, MB(44),  OatKind::Mmio },
};

/* Boot-handoff stack: top of the kernel-cached DDR window. */
constexpr uint32_t kInitStackTopPa = 0x10000000u + MB(384);

class HmiKtp400MobilePageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t HmiKtp400MobilePageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "HmiKtp400MobilePageTableBuilder::VaToPa: VA 0x%08X is outside "
            "every KTP400 OAL OEMAddressTable span (nk.exe 0x803012FC)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion>
HmiKtp400MobilePageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
HmiKtp400MobilePageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        regions.push_back({ e.va_base, e.pa_base, e.size, PAGE_READWRITE });
    }

    /* i.MX6 on-chip memories, IMX6SDLRM Rev.1 Table 2-2. The OAL aliases these
       through several OAT VA bands; back the physical pages once (EmulatedMemory
       keys on PA and fatals on overlap) and let every VA alias resolve here.

       Boot ROM (96 KB mask ROM at PA 0): the OAL reads ROM+0x48 and uses it as a
       bounds-checked string-table index (cmp #0x15 at nk.exe+0xDAB2). We have no
       dump of the on-chip mask ROM, so back it as zero-filled RAM â€” index 0 is a
       valid, in-range selection. */
    regions.push_back({ 0x9A400000u, 0x00000000u, 0x00018000u, PAGE_READWRITE });


    /* OCRAM, 128 KB on i.MX6 Solo/DL at PA 0x00900000.  Linux models this as
       sram@900000 and QEMU exposes it as imx6.ocram; leave it available for
       the SDMA CCB/BD area selected by the BSP. */
    regions.push_back({ 0x9AD00000u, 0x00900000u, 0x00020000u, PAGE_READWRITE });

    return regions;
}

std::vector<DramRegion>
HmiKtp400MobilePageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

/* The CE kernel probes above the populated 384 MB DDR window by temporarily
   mapping candidate physical pages at VA 0x9D4A0000. Real unpopulated DDR
   returns open-bus data; model that instead of letting the generic unbacked-PA
   path mistake the probe for a missing MMIO peripheral and halt CERF. */
class HmiKtp400MobileUnpopulatedDram : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x28000000u; }
    uint32_t MmioSize() const override { return 0x38000000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t, uint8_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

class HmiKtp400MobileMicroOmsWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x60000000u; }
    uint32_t MmioSize() const override { return 0x10000000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t, uint8_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

class HmiKtp400MobileHighOpenBus : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x70000000u; }
    uint32_t MmioSize() const override { return 0x10000000u; }

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t, uint8_t) override {}
    void WriteHalf(uint32_t, uint16_t) override {}
    void WriteWord(uint32_t, uint32_t) override {}
};

/* i.MX6 Solo memory map exposes the WEIM external-memory chip-select aperture
   below MMDC DDR; on this BSP late code reaches PA 0x08000000 directly after
   ENET init.  Linux models the WEIM controller at 0x021B8000 and the i.MX6
   reference map places external CS windows in this low physical range.  No
   decoded KTP400 companion register semantics are known for this aperture yet,
   so model the electrical bus faithfully enough for probe/config writes:
   unwired reads return open bus, writes are accepted into a small shadow. */
class HmiKtp400MobileWeimCs0Window : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 0x08000000u; }  /* 128 MB */

    uint8_t  ReadByte(uint32_t) override { return 0xFFu; }
    uint16_t ReadHalf(uint32_t) override { return 0xFFFFu; }
    uint32_t ReadWord(uint32_t) override { return 0xFFFFFFFFu; }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Store(addr, value, 1);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Store(addr, value, 2);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        Store(addr, value, 4);
    }

private:
    void Store(uint32_t addr, uint32_t value, uint32_t bytes) {
        const uint32_t off = addr - MmioBase();
        for (uint32_t i = 0; i < bytes; ++i)
            shadow_[(off + i) & 0xFFu] = static_cast<uint8_t>(value >> (i * 8u));
    }

    uint8_t shadow_[0x100] = {};
};

class HmiKtp400MobileBootPlacer : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }

    void PlaceAfterRom() override {
        auto& mem = emu_.Get<EmulatedMemory>();

        /* Restore/verify the OAL OEMAddressTable exactly as decoded from nk.exe.

           This is the BSP/OAL table the board uses for peripheral VA aliases.
           Do not append 0x60000000 and do not rebuild a synthetic 12-byte DDR
           table here.  The word at 0x8030133C is both the terminator/header
           magic after the 16-byte OAL table and the pointer passed by start()
           to sub_8030D7A4, so only the OAL table body and the magic word are
           touched.  Bytes after the magic/header belong to nk.exe and are left
           exactly as the ROM image placed them. */
        struct RawOalOatEntry {
            uint32_t va;
            uint32_t pa;
            uint32_t size;
            uint32_t flags;
        };

        constexpr uint32_t kOalOatPa      = 0x103012FCu;
        constexpr uint32_t kOalOatMagicPa = 0x1030133Cu;
        constexpr uint32_t kOalOatMagic   = 0x87654321u;
        static constexpr RawOalOatEntry kOalOat[] = {
            { 0x98000000u, 0x00100000u, MB(3),  0x00000C00u },
            { 0x98300000u, 0x000C0000u, MB(33), 0x00000C00u },
            { 0x9A400000u, 0x00000000u, MB(44), 0x00000C00u },
            { 0x00000000u, 0x00000000u, 0u,     0x00000000u },
        };

        uint32_t old_words[4 * (sizeof(kOalOat) / sizeof(kOalOat[0]))];
        for (uint32_t i = 0; i < sizeof(old_words) / sizeof(old_words[0]); ++i)
            old_words[i] = mem.ReadWord(kOalOatPa + i * 4u);
        const uint32_t old_magic = mem.ReadWord(kOalOatMagicPa);

        for (uint32_t i = 0; i < sizeof(kOalOat) / sizeof(kOalOat[0]); ++i) {
            const uint32_t pa = kOalOatPa + i * 16u;
            mem.WriteWord(pa + 0x0u, kOalOat[i].va);
            mem.WriteWord(pa + 0x4u, kOalOat[i].pa);
            mem.WriteWord(pa + 0x8u, kOalOat[i].size);
            mem.WriteWord(pa + 0xCu, kOalOat[i].flags);
        }
        mem.WriteWord(kOalOatMagicPa, kOalOatMagic);

        LOG(Boot, "HmiKtp400MobileBootPlacer: restored OAL OEMAddressTable "
                  "PA 0x%08X: [98000000->00100000 size=00300000 flags=00000C00; "
                  "98300000->000C0000 size=02100000 flags=00000C00; "
                  "9A400000->00000000 size=02C00000 flags=00000C00; zero slot; "
                  "magic@0x%08X=%08X] old_first=[%08X %08X %08X %08X] "
                  "old_magic=%08X; no synthetic 0x60000000 OAT entry\n",
            kOalOatPa, kOalOatMagicPa, kOalOatMagic,
            old_words[0], old_words[1], old_words[2], old_words[3], old_magic);

        /* sub_8030E8E8 (OALMpInit) copies dword_80303AA4 into the
           OAL callback-table gate at [OAL_ARGS + 0x174].  KTP400 Mobile is an
           i.MX6 Solo target: one Cortex-A9 core is visible to the BSP, and the
           boot log reports Num CPUs = 1 / SMP Enabled = 0.  A real bootloader
           must not advertise the MP callback vector for this board.  Earlier
           CERF builds either patched [OAL_ARGS+0x18C] directly from the JIT or
           forced this flag to 1; both alter guest policy.  Keep the single-core
           bootloader contract explicit and leave OAL to skip the MP callback
           table on its own. */
        constexpr uint32_t kOalMpEnableFlagPa = 0x10303AA4u;
        mem.WriteWord(kOalMpEnableFlagPa, 0u);
        LOG(Boot, "HmiKtp400MobileBootPlacer: i.MX6 Solo single-core handoff: "
                  "OALMpInit enable flag PA 0x%08X = 0; no MP callback patch\n",
            kOalMpEnableFlagPa);

        /* Siemens' OAL exposes the MicroOMS hardware-info blob through custom
           KernelIoControl codes 0x01014090 (GET) and 0x01014094 (SET).  The
           decompiled OAL does not require a fixed VA for the buffer.  It reads
           a boot-loader handoff PA from VA 0x838767AC, translates it with
           OALPAtoVA(), and caches the resulting VA at 0x83877B10.

           The clean boot log proves the live value consumed by the OAL GET
           handler is PA 0x10005000:
             GET.entry ... slotPA=+00=10005000(pa=138767AC)
           The earlier 0x13BA0000 seed stayed unused, so BSPIO copied a zero
           HWF envelope, failed MicroOMS initialization, and GWES later reached
           the display path with a null surface object.  Seed the bootloader PA
           actually advertised by OAL instead of a synthetic high-RAM buffer.

           Do not patch 0x83877B10 here, do not patch BSPIO, and do not force
           MapViewOfFile to a synthetic VA.  BSPIO still uses the real OAL
           0x01014090 GET path; it now receives the firmware handoff bytes from
           the same PA that OAL maps. */
        constexpr uint32_t kOalHwInfoPaSlotVa = 0x838767ACu;
        constexpr uint32_t kHwInfoHandoffPa   = 0x10005000u;
        constexpr uint32_t kHwInfoSeedClear   = 0x00000200u;

        const auto va_to_pa = [](uint32_t va) {
            return 0x10000000u + (va - 0x80000000u);
        };

        mem.WriteWord(va_to_pa(kOalHwInfoPaSlotVa), kHwInfoHandoffPa);
        /* PA 0x10005000 is the low-DDR handoff buffer used by the real OAL.
           Do not blanket-clear the previous high-RAM 128 KiB range here: on the
           real handoff PA only the HWF envelope prefix is owned by this seed,
           and later OAL SET calls may update the buffer. */
        for (uint32_t i = 0; i < kHwInfoSeedClear; ++i)
            mem.WriteByte(kHwInfoHandoffPa + i, 0u);

        /* The decompiled BSPIO cold path proves that the first
           CreateFileMapping/MapViewOfFile user mapping is initialized from
           IOCTL_HAL_GET_XXX_MEM (0x01014090), before ReloadHardwareInfoFromSSD
           can repair/update it from the eMMC sector table:

             k.bspio.dll EF5C6D64:
               memset(map, 0, 102400)
               KernelIoControl(0x01014090, out=&size, outLen=4)
               KernelIoControl(0x01014090, out=map, outLen=size+8)
               parse map+9

           Therefore the bootloader-owned PA published above must already carry
           the same HWF envelope that the eMMC seed later exposes through the
           factory table at 0x101000.  This models firmware handoff data, not a
           BSPIO/MapView shortcut: OAL still owns 0x83877B10 and copies from this
           PA through its real GET/SET handlers. */
        constexpr uint32_t kHwfToken = 0x4B545034u;  /* "4PTK" little-endian */
        const std::vector<uint8_t> ktp400_oms_root =
            BuildKtp400HardwareInfoOms(
                emu_.Get<NetworkBackend>().GuestMacAddress());
        const uint32_t kHwfSize =
            static_cast<uint32_t>(ktp400_oms_root.size()) + 1u;

        const auto write_le32_pa = [&](uint32_t pa, uint32_t value) {
            mem.WriteByte(pa + 0u, static_cast<uint8_t>( value        & 0xFFu));
            mem.WriteByte(pa + 1u, static_cast<uint8_t>((value >>  8u) & 0xFFu));
            mem.WriteByte(pa + 2u, static_cast<uint8_t>((value >> 16u) & 0xFFu));
            mem.WriteByte(pa + 3u, static_cast<uint8_t>((value >> 24u) & 0xFFu));
        };

        write_le32_pa(kHwInfoHandoffPa + 0x00u, kHwfSize);
        write_le32_pa(kHwInfoHandoffPa + 0x04u, kHwfToken);
        mem.WriteByte(kHwInfoHandoffPa + 0x08u, 0u);
        for (uint32_t i = 0; i < ktp400_oms_root.size(); ++i)
            mem.WriteByte(kHwInfoHandoffPa + 0x09u + i, ktp400_oms_root[i]);

        LOG(Boot, "HmiKtp400MobileBootPlacer: MicroOMS HW-info boot handoff "
                  "[VA 0x%08X] = live OAL PA 0x%08X clear=0x%08X seeded_hwf=%u "
                  "(OAL owns 0x83877B10; BSPIO/MapView unpatched)\n",
            kOalHwInfoPaSlotVa, kHwInfoHandoffPa, kHwInfoSeedClear, kHwfSize);

    }
};

}  /* namespace */

REGISTER_SERVICE_AS(HmiKtp400MobilePageTableBuilder, PageTableBuilder);
REGISTER_SERVICE(HmiKtp400MobileUnpopulatedDram);
REGISTER_SERVICE(HmiKtp400MobileMicroOmsWindow);
REGISTER_SERVICE(HmiKtp400MobileHighOpenBus);
REGISTER_SERVICE(HmiKtp400MobileWeimCs0Window);
REGISTER_SERVICE_AS(HmiKtp400MobileBootPlacer, BoardBootPlacer);
