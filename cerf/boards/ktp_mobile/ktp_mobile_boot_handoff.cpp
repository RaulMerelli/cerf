#include "ktp_mobile_boot_handoff.h"

#include "ktp_mobile_oat.h"
#include "ktp_mobile_oat_from_rom.h"

#include "../../boot/rom_parser_service.h"

#include "../board_context.h"
#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../net/network_backend.h"
#include "../../peripherals/sd_card/ktp_mobile_hardware_info.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/guest_cpu_reset.h"

#include <vector>

namespace {

constexpr uint32_t kOalOatMagic = 0x87654321u;
constexpr uint32_t kHwInfoHandoffPa = 0x10005000u;
constexpr uint32_t kHwInfoSeedClear = 0x00000200u;
constexpr uint32_t kHwfToken = 0x4B545034u; /* "4PTK" little-endian */

} /* namespace */

bool KtpMobileBootHandoff::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && BoardContext::IsKtpMobile(bd->GetBoard());
}

void KtpMobileBootHandoff::OnAllReady() {
    /* CERF enters the OS image after the real first-stage loader. The i.MX6
       manual requires pre-OS software to clear both reset-enabled 16-second
       WDOG power-down counters; the WinCE image demonstrably starts after that
       handoff and does not repeat it. Reapply the same loader-owned handoff
       after CERF's direct-to-kernel reset path. */
    CompleteWatchdogBootHandoff();
    emu_.Get<GuestCpuReset>().RegisterPostResetListener(
        [this](ResetLineKind) { CompleteWatchdogBootHandoff(); });
}

void KtpMobileBootHandoff::CompleteWatchdogBootHandoff() {
    auto& mmio = emu_.Get<PeripheralDispatcher>();
    mmio.WriteHalf(0x020BC008u, 0u);
    mmio.WriteHalf(0x020C0008u, 0u);
}

void KtpMobileBootHandoff::Place(const KtpMobileOalLayout& oal) {
    auto& mem = emu_.Get<EmulatedMemory>();
    auto& ptb = emu_.Get<PageTableBuilder>();

    /* The OEMAddressTable is read out of the ROM itself rather than recorded
       per build: it ends with a zero entry followed by 0x87654321 and the VA
       of the table, so every image declares where it is and what it contains.
       The V13 and V17 Mobile Panel images differ in both. */
    auto& parser = emu_.Get<RomParserService>();
    if (!parser.Ok()) {
        LOG(Caution, "%s: ROM not parsed; OAL handoff skipped\n", oal.log_tag);
        return;
    }
    const KtpMobileRomOat rom_oat = FindKtpMobileOatInRom(parser.Primary().flat);
    if (!rom_oat.valid()) {
        LOG(Caution, "%s: no OAL OEMAddressTable found in the ROM\n", oal.log_tag);
        return;
    }
    const uint32_t oat_pa = ptb.VaToPa(rom_oat.table_va);
    const uint32_t oat_magic_pa = ptb.VaToPa(rom_oat.magic_va);

    const KtpMobileRomOalWords words = FindKtpMobileOalWordsInRom(parser.Primary().flat, rom_oat.base_va);
    if (!words.valid()) {
        LOG(Caution,
            "%s: the OAL hardware-info reader was not found in the "
            "ROM; the MicroOMS handoff is skipped\n",
            oal.log_tag);
        return;
    }

    /* Restore/verify the OAL OEMAddressTable exactly as decoded from nk.exe.

       This is the BSP/OAL table the board uses for peripheral VA aliases.
       Do not append 0x60000000 and do not rebuild a synthetic 12-byte DDR
       table here.  The word at the magic address is both the terminator/header
       magic after the 16-byte OAL table and the pointer passed by start() to
       the OAL init path, so only the OAL table body and the magic word are
       touched.  Bytes after the magic/header belong to nk.exe and are left
       exactly as the ROM image placed them. */
    uint32_t old_words[4];
    for (uint32_t i = 0; i < 4u; ++i)
        old_words[i] = mem.ReadWord(oat_pa + i * 4u);
    const uint32_t old_magic = mem.ReadWord(oat_magic_pa);

    for (size_t i = 0; i < rom_oat.entries.size(); ++i) {
        const uint32_t pa = oat_pa + static_cast<uint32_t>(i) * 16u;
        mem.WriteWord(pa + 0x0u, rom_oat.entries[i].va);
        mem.WriteWord(pa + 0x4u, rom_oat.entries[i].pa);
        mem.WriteWord(pa + 0x8u, rom_oat.entries[i].size);
        mem.WriteWord(pa + 0xCu, rom_oat.entries[i].flags);
    }
    const uint32_t zero_pa = oat_pa + static_cast<uint32_t>(rom_oat.entries.size()) * 16u;
    for (uint32_t i = 0; i < 4u; ++i)
        mem.WriteWord(zero_pa + i * 4u, 0u);
    mem.WriteWord(oat_magic_pa, kOalOatMagic);

    LOG(Boot,
        "%s: restored the OAL OEMAddressTable the ROM declares at "
        "PA 0x%08X: %zu entries + zero slot, first %08X->%08X "
        "size=%08X flags=%08X, magic@0x%08X=%08X; "
        "old_first=[%08X %08X %08X %08X] old_magic=%08X\n",
        oal.log_tag, oat_pa, rom_oat.entries.size(), rom_oat.entries[0].va, rom_oat.entries[0].pa,
        rom_oat.entries[0].size, rom_oat.entries[0].flags, oat_magic_pa, kOalOatMagic, old_words[0], old_words[1],
        old_words[2], old_words[3], old_magic);

    /* Siemens' OAL exposes the MicroOMS hardware-info blob through custom
       KernelIoControl codes 0x01014090 (GET) and 0x01014094 (SET).  The
       decompiled OAL does not require a fixed VA for the buffer.  It reads a
       boot-loader handoff PA from the slot below, translates it with
       OALPAtoVA(), and caches the resulting VA in the cache slot.

       The clean boot log proves the live value consumed by the OAL GET
       handler is PA 0x10005000:
         GET.entry ... slotPA=+00=10005000(pa=138767AC) */
    mem.WriteWord(ptb.VaToPa(words.hw_info_slot_va), kHwInfoHandoffPa);
    /* PA 0x10005000 is the low-DDR handoff buffer used by the real OAL; only
       the HWF envelope prefix is owned by this seed, and later OAL SET calls
       may update the buffer. */
    for (uint32_t i = 0; i < kHwInfoSeedClear; ++i)
        mem.WriteByte(kHwInfoHandoffPa + i, 0u);

    /* The decompiled BSPIO cold path proves that the first
       CreateFileMapping/MapViewOfFile user mapping is initialized from
       IOCTL_HAL_GET_XXX_MEM (0x01014090), before ReloadHardwareInfoFromSSD can
       repair/update it from the eMMC sector table:

         k.bspio.dll EF5C6D64:
           memset(map, 0, 102400)
           KernelIoControl(0x01014090, out=&size, outLen=4)
           KernelIoControl(0x01014090, out=map, outLen=size+8)
           parse map+9

       Therefore the bootloader-owned PA published above must already carry the
       same HWF envelope that the eMMC seed later exposes through the factory
       table at 0x101000. */
    const std::vector<uint8_t> oms_root =
        BuildKtpMobileHardwareInfoOms(emu_.Get<NetworkBackend>().GuestMacAddress(), oal.op_type, oal.panel);
    const uint32_t hwf_size = static_cast<uint32_t>(oms_root.size()) + 1u;

    const auto write_le32_pa = [&](uint32_t pa, uint32_t value) {
        mem.WriteByte(pa + 0u, static_cast<uint8_t>(value & 0xFFu));
        mem.WriteByte(pa + 1u, static_cast<uint8_t>((value >> 8u) & 0xFFu));
        mem.WriteByte(pa + 2u, static_cast<uint8_t>((value >> 16u) & 0xFFu));
        mem.WriteByte(pa + 3u, static_cast<uint8_t>((value >> 24u) & 0xFFu));
    };

    write_le32_pa(kHwInfoHandoffPa + 0x00u, hwf_size);
    write_le32_pa(kHwInfoHandoffPa + 0x04u, kHwfToken);
    mem.WriteByte(kHwInfoHandoffPa + 0x08u, 0u);
    for (uint32_t i = 0; i < oms_root.size(); ++i)
        mem.WriteByte(kHwInfoHandoffPa + 0x09u + i, oms_root[i]);

    LOG(Boot,
        "%s: MicroOMS HW-info boot handoff [VA 0x%08X] = live OAL "
        "PA 0x%08X clear=0x%08X seeded_hwf=%u (OAL owns 0x%08X; "
        "BSPIO/MapView unpatched)\n",
        oal.log_tag, words.hw_info_slot_va, kHwInfoHandoffPa, kHwInfoSeedClear, hwf_size, words.hw_info_cache_va);
}

REGISTER_SERVICE(KtpMobileBootHandoff);
