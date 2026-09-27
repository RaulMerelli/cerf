#include "ktp_mobile_boot_handoff.h"

#include "ktp_mobile_oat.h"
#include "ktp_mobile_oat_from_rom.h"

#include "../../boot/rom_parser_service.h"

#include "../board_context.h"
#include "../page_table_builder.h"

#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../net/network_backend.h"
#include "ktp_mobile_hardware_info.h"
#include "../../socs/imx6/imx6_fec.h"

#include <vector>
#include "ktp_mobile_id.h"

namespace {

constexpr uint32_t kOalOatMagic = 0x87654321u;
constexpr uint32_t kHwInfoHandoffPa = 0x10005000u;
constexpr uint32_t kHwInfoSeedClear = 0x00000200u;
constexpr uint32_t kHwfToken = 0x4B545034u;

}

bool KtpMobileBootHandoff::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && BoardId::IsKtpMobile(bd->GetBoardId());
}

void KtpMobileBootHandoff::Place(const KtpMobileOalLayout& oal) {
    auto& mem = emu_.Get<EmulatedMemory>();
    auto& ptb = emu_.Get<PageTableBuilder>();

    /* nk.exe OEMAddressTable terminator: zero entry, 0x87654321, then table VA. */
    auto& parser = emu_.Get<RomParserService>();
    if (!parser.Ok())
        emu_.Get<Fatal>().Die("%s: ROM not parsed; the OAL handoff cannot be placed", oal.log_tag);
    const KtpMobileRomOat rom_oat = FindKtpMobileOatInRom(parser.Primary().flat);
    if (!rom_oat.valid())
        emu_.Get<Fatal>().Die("%s: no OAL OEMAddressTable found in the ROM", oal.log_tag);
    const uint32_t oat_pa = ptb.VaToPa(rom_oat.table_va);
    const uint32_t oat_magic_pa = ptb.VaToPa(rom_oat.magic_va);

    const KtpMobileRomOalWords words = FindKtpMobileOalWordsInRom(parser.Primary().flat, rom_oat.base_va);
    if (!words.valid())
        emu_.Get<Fatal>().Die("%s: the OAL hardware-info reader was not found in the ROM", oal.log_tag);

    /* nk.exe OEMAddressTable body ends immediately before its 0x87654321 header. */
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

    /* nk.exe IOCTL 0x01014090 reads the MicroOMS blob through an OALPAtoVA-translated PA. */
    mem.WriteWord(ptb.VaToPa(words.hw_info_slot_va), kHwInfoHandoffPa);
    for (uint32_t i = 0; i < kHwInfoSeedClear; ++i)
        mem.WriteByte(kHwInfoHandoffPa + i, 0u);

    /* hmi_ktp400_mobile_v13 bspio.dll @0x41885AC0 reads IOCTL 0x01014090 before reloading hardware info. */
    const std::vector<uint8_t> oms_root =
        BuildKtpMobileHardwareInfoOms(
            emu_.Get<NetworkBackend>().MacForReceiver(kImx6FecReceiverId,
                                                      NetworkBackend::ReceiverKind::Ethernet),
            oal.op_type, oal.panel);
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
