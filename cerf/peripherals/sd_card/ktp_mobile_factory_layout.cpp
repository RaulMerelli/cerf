#include "ktp_mobile_factory_layout.h"

#include "../../boot/fwf_oms_reader.h"

#include "ktp_mobile_hardware_info.h"

#include "../../core/log.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {

constexpr uint32_t kKtp400FactoryTableOff = 0x00101000u;
constexpr uint32_t kKtp400PartLbaBytes = 0x00000200u;
constexpr uint32_t kKtp400FwfInfoOff = 0x02000000u;
constexpr uint32_t kKtp400FwfInfoSize = 0x04000000u;

void Put32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}

uint32_t Get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8u) | (uint32_t(p[2]) << 16u) | (uint32_t(p[3]) << 24u);
}

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

std::vector<uint8_t> ReadWholeFile(const std::string& path, size_t max_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0 || (max_size != 0 && static_cast<uint64_t>(size) > max_size)) return {};
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(blob.data()), size);
    if (in.good() || in.gcount() == size) return blob;
    return {};
}

} // namespace

namespace ktp_mobile_emmc {

std::vector<uint8_t> LoadFwfContainer(const std::string& device_dir, const std::string& container_name) {
    std::vector<uint8_t> container = container_name.empty()
                                         ? std::vector<uint8_t>()
                                         : ReadWholeFile(device_dir + container_name, 256u * 1024u * 1024u);
    if (container.empty()) {
        LOG(Caution,
            "LoadFwfContainer: %s%s absent or unreadable; installed "
            "firmware metadata will be unavailable\n",
            device_dir.c_str(), container_name.c_str());
    }
    return container;
}

void EnsureFactoryLayout(std::vector<uint8_t>& data, const std::vector<uint8_t>& fwf_container,
                         const std::array<uint8_t, 6>& hardware_mac, KtpMobileOpType op_type, KtpMobilePanel panel) {
    /* Siemens BSPIO.dll initializes MicroOMS hardware info by reading the eMMC
       store directly:

         hmi_ktp400_mobile_v13 bspio.dll 41885AC0:
           read 512 bytes at byte offset 0x101000 into dword_4188F610
           require table[0]=0x20100305, table[1]=512, table[2]=0

         hmi_ktp400_mobile_v13 bspio.dll 41886014 / 418860C4:
           table+0x38 = boot-state byte offset
           table+0x3C = boot-state area size

         hmi_ktp400_mobile_v13 bspio.dll 41886264 / 41886164:
           table+0x40 = HWF byte offset
           table+0x44 = HWF area size, must be 512-byte aligned
           HWF[0..3]  = serialized HW-info size
           HWF[4..7]  = CRC32 of HWF[8..8+size)
           HWF+8      = OMS stream version (03)
           HWF+9      = OMS root object (A1...) parsed by sub_4188C1A8.

         hmi_ktp400_mobile_v13 bspio.dll 41885F3C / 41885E60:
           table+0x48 = PA-header byte offset
           table+0x4C = PA-header area size, must be >= 512 bytes
           PA[0..7]   = "PAHD", version 1.  If absent but the table entry is
                        valid, BSPIO creates this default header itself.

       Real panels ship with a populated Siemens HW-info sector area.  Seed the
       synthetic eMMC with the FWF-derived MicroOMS tree plus the MAC subtree
       consumed by HWI_GetMACAddressInfo, so BSPIO can initialize the panel and
       return the same address exposed by the network backend. */
    constexpr uint32_t kSectorTableOff = kKtp400FactoryTableOff;
    constexpr uint32_t kPartLbaBytes = kKtp400PartLbaBytes;
    constexpr uint32_t kSectorMagic = 0x20100305u;
    constexpr uint32_t kHwfOff = 0x00102000u;
    constexpr uint32_t kHwfAreaSize = 0x00000200u;
    constexpr uint32_t kBootStateOff = 0x00103000u;
    constexpr uint32_t kBootStateSize = 0x00000200u;
    constexpr uint32_t kPaHeaderOff = 0x00103200u;
    constexpr uint32_t kPaHeaderSize = 0x00000200u;
    constexpr uint32_t kFwfInfoOff = kKtp400FwfInfoOff;
    constexpr uint32_t kFwfInfoSize = kKtp400FwfInfoSize;
    const std::vector<uint8_t> ktp400_oms_root = BuildKtpMobileInstalledHardwareDescriptionOms(hardware_mac, op_type);
    std::vector<uint8_t> installed_firmware;
    cerf::fwf_oms::ExtractInstalledFirmwareSummary(fwf_container.data(), fwf_container.size(), installed_firmware);
    (void)panel;
    const uint32_t kHwfSize = static_cast<uint32_t>(ktp400_oms_root.size());

    const auto seed_at = [&](uint32_t table_off, uint32_t hwf_off, uint32_t boot_state_off, uint32_t pa_header_off,
                             uint32_t fwf_info_off) {
        if (data.size() < hwf_off + kHwfAreaSize || data.size() < boot_state_off + kBootStateSize ||
            data.size() < pa_header_off + kPaHeaderSize || data.size() < fwf_info_off + kFwfInfoSize ||
            data.size() < table_off + 512u)
            return;

        uint8_t* table = data.data() + table_off;
        /* Treat this as the factory sector table, not as mutable user data.
           Old synthetic backings can contain a formally valid table whose HWF
           offsets point at blank media; BSPIO then reports "sizeOfHwf is 0" and
           the panel identity/brightness tree never appears.  Real Siemens
           media ships with a self-consistent table, so keep our synthetic one
           deterministic on every cold boot/load. */
        std::memset(table, 0, 512u);
        Put32(table + 0x00u, kSectorMagic);
        Put32(table + 0x04u, 512u);
        Put32(table + 0x08u, 0u);
        Put32(table + 0x38u, boot_state_off);
        Put32(table + 0x3Cu, kBootStateSize);
        Put32(table + 0x40u, hwf_off);
        Put32(table + 0x44u, kHwfAreaSize);
        Put32(table + 0x48u, pa_header_off);
        Put32(table + 0x4Cu, kPaHeaderSize);
        Put32(table + 0x50u, installed_firmware.empty() ? 0u : fwf_info_off);
        Put32(table + 0x54u, installed_firmware.empty() ? 0u : kFwfInfoSize);

        uint8_t* boot = data.data() + boot_state_off;
        if (Get32(boot) == 0u) Put32(boot, 0x96969664u); /* BSPIO's no-state sentinel. */

        uint8_t* pa = data.data() + pa_header_off;
        if (!(Get32(pa + 0x00u) == 0x44484150u && Get32(pa + 0x04u) == 1u)) {
            std::memset(pa, 0, kPaHeaderSize);
            Put32(pa + 0x00u, 0x44484150u); /* "PAHD" */
            Put32(pa + 0x04u, 1u);
        }

        uint8_t* hwf = data.data() + hwf_off;
        std::memset(hwf, 0, kHwfAreaSize);
        Put32(hwf + 0x00u, kHwfSize);
        /* DeviceManager.exe 0x14B9E skips the OMS version byte before calling
           Object::import_from_blob.  BSPIO.dll 0x41886DC0 likewise starts its
           object parser at HWF+9. */
        std::memcpy(hwf + 0x08u, ktp400_oms_root.data(), ktp400_oms_root.size());
        /* hmi_ktp400_mobile_v13 dmosapi.dll 0x418AD4A0 validates the
           CDmOsLinearStoreBin header before exposing /hwf. */
        Put32(hwf + 0x04u, Crc32(hwf + 0x08u, kHwfSize));

        uint8_t* fwf = data.data() + fwf_info_off;
        if (!installed_firmware.empty() && installed_firmware.size() + 8u <= kFwfInfoSize) {
            /* Derive the installed Firmware OMS tree from the FWF's
               PersistentStream, retaining its identity/version but excluding
               image and update containers.  The
               linear-store envelope is the same [size, CRC32, OMS stream]
               shape used by /hwf.  Publishing the complete PersistentStream here makes
               place_sft treat every cold boot as an OS update; publishing no
               /fwf leaves DeviceManager unable to describe installed firmware. */
            std::memset(fwf, 0, installed_firmware.size() + 8u);
            Put32(fwf + 0x00u, static_cast<uint32_t>(installed_firmware.size()));
            Put32(fwf + 0x04u, Crc32(installed_firmware.data(), installed_firmware.size()));
            std::memcpy(fwf + 0x08u, installed_firmware.data(), installed_firmware.size());
        }
    };

    seed_at(kSectorTableOff, kHwfOff, kBootStateOff, kPaHeaderOff, kFwfInfoOff);

    /* BSPIO uses byte offsets from its Store abstraction.  Depending on whether
       CE has opened the whole eMMC or the first MBR partition, offset 0x101000
       can be absolute-card or partition-relative.  The synthetic KTP400 media
       has partition LBA=1, so mirror the Siemens HW-info sector area one block
       later as well. */
    seed_at(kSectorTableOff + kPartLbaBytes, kHwfOff + kPartLbaBytes, kBootStateOff + kPartLbaBytes,
            kPaHeaderOff + kPartLbaBytes, kFwfInfoOff + kPartLbaBytes);

    /* Some CE storage paths expose the partition as the store base but still
       pass byte offsets through another layer that adds the partition LBA.
       Mirror the table with unshifted payload offsets too, while keeping the
       payload itself present at both absolute and +LBA locations. */
    seed_at(kSectorTableOff + kPartLbaBytes, kHwfOff, kBootStateOff, kPaHeaderOff, kFwfInfoOff);
}

} // namespace ktp_mobile_emmc
