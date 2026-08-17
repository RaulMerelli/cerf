#include "ktp400_factory_layout.h"

#include "ktp400_hardware_info.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {

constexpr uint32_t kKtp400FactoryTableOff = 0x00101000u;
constexpr uint32_t kKtp400PartLbaBytes = 0x00000200u;
constexpr uint32_t kKtp400FwfInfoOff = 0x02000000u;
constexpr uint32_t kKtp400FwfInfoSize = 0x04000000u;
constexpr uint32_t kKtp400FwfPayload0Off = 0x02000000u;
constexpr uint32_t kKtp400FwfPayload1Off = 0x02800000u;
constexpr uint32_t kKtp400FwfPayload0Len = 0x00800000u;
constexpr uint32_t kKtp400FwfPayload1Len = 0x0056219Cu;

void Put32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
    p[2] = static_cast<uint8_t>(value >> 16u);
    p[3] = static_cast<uint8_t>(value >> 24u);
}

uint32_t Get32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8u) |
           (uint32_t(p[2]) << 16u) | (uint32_t(p[3]) << 24u);
}

std::vector<uint8_t> ReadWholeFile(const char* path, size_t max_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0 || (max_size != 0 && static_cast<uint64_t>(size) > max_size))
        return {};
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(blob.data()), size);
    if (in.good() || in.gcount() == size) return blob;
    return {};
}

size_t FindFsfPayloadOffset(const std::vector<uint8_t>& stream) {
    constexpr uint8_t kFsfMagic[] = {'F', 'S', 'F', 0x00u};
    const auto it = std::search(stream.begin(), stream.end(),
                                std::begin(kFsfMagic), std::end(kFsfMagic));
    return it == stream.end() ? 0u : static_cast<size_t>(it - stream.begin());
}

size_t FindNextInPlaceMarker(const std::vector<uint8_t>& stream, size_t start) {
    constexpr uint8_t kMarker[] = {
        'I','n','P','l','a','c','e','B','l','o','b','S','t','r','e','a','m','e','d'
    };
    if (start >= stream.size()) return 0u;
    const auto it = std::search(stream.begin() + start, stream.end(),
                                std::begin(kMarker), std::end(kMarker));
    return it == stream.end() ? 0u : static_cast<size_t>(it - stream.begin());
}

}

namespace ktp400_emmc {

FwfAssets LoadFwfAssets() {
    return {
        ReadWholeFile("devices\\hmi_ktp400_mobile_v13\\fwf_info_stream.bin",
                      32u * 1024u * 1024u),
        ReadWholeFile("devices\\hmi_ktp400_mobile_v13\\fwf_raw_blob_1.bin",
                      0x40000u)
    };
}

void EnsureFactoryLayout(std::vector<uint8_t>& data,
                         const std::vector<uint8_t>& fwf_info_stream,
                         const std::vector<uint8_t>& fwf_info_fallback_object,
                         const std::array<uint8_t, 6>& hardware_mac) {
    /* Siemens BSPIO.dll initializes MicroOMS hardware info by reading the eMMC
       store directly:

         IDA bspio.dll 41885AC0:
           read 512 bytes at byte offset 0x101000 into dword_4188F610
           require table[0]=0x20100305, table[1]=512, table[2]=0

         IDA bspio.dll 41886014 / 418860C4:
           table+0x38 = boot-state byte offset
           table+0x3C = boot-state area size

         IDA bspio.dll 41886264 / 41886164:
           table+0x40 = HWF byte offset
           table+0x44 = HWF area size, must be 512-byte aligned
           HWF[0..3]  = serialized HW-info size
           HWF[4..7]  = hash/version token mirrored into shared RAM
           HWF+9      = OMS object serialization parsed by sub_4188C1A8.

         IDA bspio.dll 41885F3C / 41885E60:
           table+0x48 = PA-header byte offset
           table+0x4C = PA-header area size, must be >= 512 bytes
           PA[0..7]   = "PAHD", version 1.  If absent but the table entry is
                        valid, BSPIO creates this default header itself.

       Real panels ship with a populated Siemens HW-info sector area.  Seed the
       synthetic eMMC with the FWF-derived MicroOMS tree plus the MAC subtree
       consumed by HWI_GetMACAddressInfo, so BSPIO can initialize the panel and
       return the same address exposed by the network backend. */
    constexpr uint32_t kSectorTableOff = kKtp400FactoryTableOff;
    constexpr uint32_t kPartLbaBytes   = kKtp400PartLbaBytes;
    constexpr uint32_t kSectorMagic    = 0x20100305u;
    constexpr uint32_t kHwfOff         = 0x00102000u;
    constexpr uint32_t kHwfAreaSize    = 0x00000200u;
    constexpr uint32_t kBootStateOff   = 0x00103000u;
    constexpr uint32_t kBootStateSize  = 0x00000200u;
    constexpr uint32_t kPaHeaderOff    = 0x00103200u;
    constexpr uint32_t kPaHeaderSize   = 0x00000200u;
    constexpr uint32_t kFwfInfoOff     = kKtp400FwfInfoOff;
    constexpr uint32_t kFwfInfoSize    = kKtp400FwfInfoSize;
    constexpr uint32_t kFwfPayload0Off = kKtp400FwfPayload0Off;
    constexpr uint32_t kFwfPayload1Off = kKtp400FwfPayload1Off;
    constexpr uint32_t kFwfPayload0Len = kKtp400FwfPayload0Len;
    constexpr uint32_t kFwfPayload1Len = kKtp400FwfPayload1Len;
    constexpr uint32_t kHwfToken       = 0x4B545034u;  /* "4PTK" LE: stable non-zero token */
    constexpr uint32_t kFallbackFwfInfoToken = 0x49465746u;  /* "FWFI" LE */
    const std::vector<uint8_t> ktp400_oms_root =
        BuildKtp400HardwareInfoOms(hardware_mac);
    const uint32_t kHwfSize = static_cast<uint32_t>(ktp400_oms_root.size()) + 1u;

    const auto seed_at = [&](uint32_t table_off, uint32_t hwf_off,
                             uint32_t boot_state_off, uint32_t pa_header_off,
                             uint32_t fwf_info_off) {
        if (data.size() < hwf_off + kHwfAreaSize ||
            data.size() < boot_state_off + kBootStateSize ||
            data.size() < pa_header_off + kPaHeaderSize ||
            data.size() < fwf_info_off + kFwfInfoSize ||
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
        Put32(table + 0x50u, fwf_info_off);
        Put32(table + 0x54u, kFwfInfoSize);

        uint8_t* boot = data.data() + boot_state_off;
        if (Get32(boot) == 0u)
            Put32(boot, 0x96969664u); /* BSPIO's no-state sentinel. */

        uint8_t* pa = data.data() + pa_header_off;
        if (!(Get32(pa + 0x00u) == 0x44484150u && Get32(pa + 0x04u) == 1u)) {
            std::memset(pa, 0, kPaHeaderSize);
            Put32(pa + 0x00u, 0x44484150u); /* "PAHD" */
            Put32(pa + 0x04u, 1u);
        }

        uint8_t* hwf = data.data() + hwf_off;
        std::memset(hwf, 0, kHwfAreaSize);
        Put32(hwf + 0x00u, kHwfSize);
        Put32(hwf + 0x04u, kHwfToken);
        hwf[0x08u] = 0x00u;  /* BSPIO starts parser at HWF+9 after size-- */
        std::memcpy(hwf + 0x09u, ktp400_oms_root.data(), ktp400_oms_root.size());

        /* place_sft.exe/CFWFInfo_internal::LoadFWFInfoFromSSD uses the same
           Siemens sector table but a different entry: table+0x50/0x54, not the
           BSPIO HWF entry at +0x40/0x44.

           The V13 FWF does not carry only a standalone OMS descriptor here.
           It carries a raw FWF stream: serialized FWF info, followed by the
           InPlaceBlobStreamed payloads used by UpdateContainer.  The earlier
           extractor split the first raw blob at the next
           "InPlaceBlobStreamed" marker, which truncated the
           UpdateContainer/ImagePart tree immediately after ImagePart.1.  That
           let CFWFInfo parse just enough to create UpdateContainer01.sft, but
           place_sft then copied the catalog/zeroes instead of the FSF payload
           and StreamingFileTransferHandler failed on a garbage install.dat.

           Seed the original raw FWF info object at a reserved eMMC offset,
           byte-for-byte as the Siemens FWF stores it.  This is not the HWF
           [size][token][pad][OMS...] envelope: LoadFWFInfoFromSSD first reads
           the little-endian object size at offset 0 (0x0004003c in the V13
           KTP400 FWF) and then reads size+8 bytes from the same raw offset.

           A tempting split at the first "FSF\0" marker is wrong: that marker
           is inside an InPlaceBlobStreamed value which the FWF parser knows how
           to skip, and the later ImagePart/LinearStore descriptors still live
           inside the 0x4003c-byte FWF object.  Truncating there lets the deploy
           step start but leaves place_sft with no Hdd LinearStore payloads.

           The Hdd/LinearStore descriptors inside this object point at raw media
           offsets 0x02000000 and 0x02800000 relative to the FWF base; place_sft
           reads those bytes later to assemble UpdateContainer01.sft. */
        uint8_t* fwf = data.data() + fwf_info_off;
        std::memset(fwf, 0, kFwfInfoSize);
        if (!fwf_info_stream.empty() && fwf_info_stream.size() >= 8u) {
            const size_t fsf_off = FindFsfPayloadOffset(fwf_info_stream);
            size_t object_len = static_cast<size_t>(Get32(fwf_info_stream.data())) + 8u;
            if (object_len < 8u || object_len > fwf_info_stream.size())
                object_len = fwf_info_stream.size();
            object_len = std::min<size_t>(object_len, kFwfInfoSize);
            std::memcpy(fwf, fwf_info_stream.data(), object_len);

            if (fsf_off != 0 && fsf_off + kFwfPayload0Len <= fwf_info_stream.size() &&
                kFwfPayload0Off + kFwfPayload0Len <= kFwfInfoSize) {
                std::memcpy(fwf + kFwfPayload0Off,
                            fwf_info_stream.data() + fsf_off,
                            kFwfPayload0Len);
            }

            const size_t second_marker =
                FindNextInPlaceMarker(fwf_info_stream, fsf_off + kFwfPayload0Len);
            const size_t second_payload = second_marker != 0 ? second_marker + 30u : 0u;
            if (second_payload != 0 && second_payload + kFwfPayload1Len <= fwf_info_stream.size() &&
                kFwfPayload1Off + kFwfPayload1Len <= kFwfInfoSize) {
                std::memcpy(fwf + kFwfPayload1Off,
                            fwf_info_stream.data() + second_payload,
                            kFwfPayload1Len);
            }
        } else if (!fwf_info_fallback_object.empty() &&
                   fwf_info_fallback_object.size() + 9u <= kFwfInfoSize) {
            /* Fallback for stripped bundles: keep the old synthetic envelope so
               the boot still exposes a loud downstream SFT/dat failure instead
               of regressing to sizeOfHwf==0. */
            Put32(fwf + 0x00u, static_cast<uint32_t>(fwf_info_fallback_object.size() + 1u));
            Put32(fwf + 0x04u, kFallbackFwfInfoToken);
            fwf[0x08u] = 0x00u;
            std::memcpy(fwf + 0x09u, fwf_info_fallback_object.data(),
                        fwf_info_fallback_object.size());
        }
    };

    seed_at(kSectorTableOff, kHwfOff, kBootStateOff, kPaHeaderOff, kFwfInfoOff);

    /* BSPIO uses byte offsets from its Store abstraction.  Depending on whether
       CE has opened the whole eMMC or the first MBR partition, offset 0x101000
       can be absolute-card or partition-relative.  The synthetic KTP400 media
       has partition LBA=1, so mirror the Siemens HW-info sector area one block
       later as well. */
    seed_at(kSectorTableOff + kPartLbaBytes,
            kHwfOff + kPartLbaBytes,
            kBootStateOff + kPartLbaBytes,
            kPaHeaderOff + kPartLbaBytes,
            kFwfInfoOff + kPartLbaBytes);

    /* Some CE storage paths expose the partition as the store base but still
       pass byte offsets through another layer that adds the partition LBA.
       Mirror the table with unshifted payload offsets too, while keeping the
       payload itself present at both absolute and +LBA locations. */
    seed_at(kSectorTableOff + kPartLbaBytes,
            kHwfOff,
            kBootStateOff,
            kPaHeaderOff,
            kFwfInfoOff);
}

}
