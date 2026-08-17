#include "sd_card_media.h"

#include "ktp400_factory_layout.h"
#include "ktp400_pdcfs_layout.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {

constexpr const char* kKtp400PdcfsBacking = "ktp400_pdcfs_autobacking.bin";
constexpr uint32_t kKtp400FactoryTableOff = 0x00101000u;
constexpr uint32_t kKtp400PartLbaBytes = 0x00000200u;
constexpr uint32_t kKtp400FwfInfoOff = 0x02000000u;
constexpr uint32_t kKtp400FwfInfoSize = 0x04000000u;
constexpr uint32_t kKtp400FwfPayload0Off = 0x02000000u;
constexpr uint32_t kKtp400FwfPayload1Off = 0x02800000u;
constexpr uint32_t kKtp400FwfPayload0Len = 0x00800000u;
constexpr uint32_t kKtp400FwfPayload1Len = 0x0056219Cu;

void Put16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8u);
}

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

struct Ktp400BackingLayout {
    bool valid = false;
    uint8_t part_type = 0;
    uint32_t part_lba = 0;
    uint32_t part_blocks = 0;
    uint16_t bytes_per_sector = 0;
    uint8_t sectors_per_cluster = 0;
    uint32_t fat32_root_cluster = 0;
};

Ktp400BackingLayout InspectKtp400Backing(const std::vector<uint8_t>& data) {
    Ktp400BackingLayout out{};
    if (data.size() < 4096u) return out;
    const uint8_t* mbr = data.data();
    if (mbr[510] != 0x55u || mbr[511] != 0xAAu) return out;
    out.part_type = mbr[450u];
    out.part_lba = Get32(mbr + 454u);
    out.part_blocks = Get32(mbr + 458u);
    if ((out.part_type != 0x0Bu && out.part_type != 0x0Cu) ||
        out.part_lba == 0u || out.part_blocks == 0u)
        return out;
    const uint64_t bpb_off = uint64_t(out.part_lba) * 512u;
    if (bpb_off + 512u > data.size() ||
        uint64_t(out.part_lba) + out.part_blocks > data.size() / 512u)
        return out;
    const uint8_t* bpb = data.data() + bpb_off;
    out.bytes_per_sector = uint16_t(bpb[11] | (uint16_t(bpb[12]) << 8u));
    out.sectors_per_cluster = bpb[13];
    out.fat32_root_cluster = Get32(bpb + 44u);
    if (out.bytes_per_sector != 512u || out.sectors_per_cluster == 0u ||
        bpb[16] == 0u || Get32(bpb + 36u) == 0u ||
        out.fat32_root_cluster < 2u || bpb[510] != 0x55u || bpb[511] != 0xAAu)
        return out;
    out.valid = true;
    return out;
}

}

SdCardMedia::SdCardMedia(uint64_t size_bytes) {
    const uint64_t blocks = (size_bytes + 511u) / 512u;
    data_.assign(static_cast<size_t>(blocks * 512u), 0u);
    auto assets = ktp400_emmc::LoadFwfAssets();
    fwf_info_stream_ = std::move(assets.info_stream);
    fwf_info_fallback_object_ = std::move(assets.fallback_object);
    BuildMinimalFat16();
}

SdCardMedia::~SdCardMedia() {
    FlushDirtyBackingRanges();
}

void SdCardMedia::SetHardwareMac(const std::array<uint8_t, 6>& mac) {
    hardware_mac_ = mac;
}

void SdCardMedia::InitializeMmcLayout() {
    if (layout_initialized_) return;
    layout_initialized_ = true;
    BuildBlankPdcfsPartition();
}

void SdCardMedia::BuildMinimalFat16() {
    const uint32_t total_blocks = static_cast<uint32_t>(data_.size() / 512u);
    if (total_blocks < 4096u) return;

    const uint32_t part_lba    = 1u;
    const uint32_t part_blocks = total_blocks - part_lba;
    uint8_t* mbr = data_.data();
    std::memset(mbr, 0, 512u);
    mbr[446 + 0] = 0x00;            /* inactive */
    mbr[446 + 1] = 0x01;            /* start CHS: head 0, sector 1, cyl 0 */
    mbr[446 + 2] = 0x01;
    mbr[446 + 3] = 0x00;
    mbr[446 + 4] = 0x06;            /* FAT16 */
    mbr[446 + 5] = 0xFE;            /* end CHS: nominal max */
    mbr[446 + 6] = 0xFF;
    mbr[446 + 7] = 0xFF;
    Put32(mbr + 446 + 8, part_lba);
    Put32(mbr + 446 + 12, part_blocks);
    mbr[510] = 0x55; mbr[511] = 0xAA;

    uint8_t* bpb = data_.data() + part_lba * 512u;
    std::memset(bpb, 0, 512u);
    bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90;
    std::memcpy(bpb + 3, "CERFSD  ", 8);
    Put16(bpb + 11, 512);           /* bytes/sector */
    bpb[13] = 2;                    /* sectors/cluster: keep 8 MB media in FAT16 range */
    Put16(bpb + 14, 1);             /* reserved sectors */
    bpb[16] = 2;                    /* FAT copies */
    Put16(bpb + 17, 512);           /* root entries */
    Put16(bpb + 19, part_blocks < 65536u ? static_cast<uint16_t>(part_blocks) : 0u);
    bpb[21] = 0xF8;                 /* fixed/removable media descriptor */
    Put16(bpb + 22, 32);            /* sectors/FAT */
    Put16(bpb + 24, 63);            /* sectors/track */
    Put16(bpb + 26, 255);           /* heads */
    Put32(bpb + 28, part_lba);      /* hidden sectors */
    Put32(bpb + 32, part_blocks >= 65536u ? part_blocks : 0u);
    bpb[36] = 0x80;                 /* drive number */
    bpb[38] = 0x29;                 /* extended boot signature */
    Put32(bpb + 39, 0x43455246u);   /* volume serial: "FREC" LE */
    std::memcpy(bpb + 43, "CERFSD     ", 11);
    std::memcpy(bpb + 54, "FAT16   ", 8);
    bpb[510] = 0x55; bpb[511] = 0xAA;

    uint8_t* fat0 = data_.data() + (part_lba + 1u) * 512u;
    uint8_t* fat1 = data_.data() + (part_lba + 33u) * 512u;
    std::memset(fat0, 0, 32u * 512u);
    std::memset(fat1, 0, 32u * 512u);
    fat0[0] = 0xF8; fat0[1] = 0xFF; fat0[2] = 0xFF; fat0[3] = 0xFF;
    fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF; fat1[3] = 0xFF;
}

void SdCardMedia::BuildBlankPdcfsPartition() {
    const uint32_t total_blocks = static_cast<uint32_t>(data_.size() / 512u);
    if (total_blocks < 4096u) return;

    std::memset(data_.data(), 0, data_.size());
    {
        std::ifstream in(kKtp400PdcfsBacking, std::ios::binary);
        if (in.good()) {
            in.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
            if (static_cast<size_t>(in.gcount()) == data_.size()) {
                /* Preserve the persisted PDCFS filesystem on load.  Validate
                   the block-0 MBR/BPB, then refresh only the reserved factory
                   HW-info sector whose MAC follows the configured backend.

                   The earlier autobacking bug was simpler: fresh synthetic
                   media worked in RAM, but sector 0 was never marked dirty, so
                   the sparse ktp400_pdcfs_autobacking.bin was created with an
                   all-zero MBR.  On the next boot mspart read LBA0 twice and
                   stopped before LBA1/PDCFS.  A structurally invalid file is not
                   repaired in place; it is discarded and regenerated from the
                   deterministic FWF/HWF seed path, equivalent to deleting the
                   broken file by hand. */
                const auto layout = InspectKtp400Backing(data_);
                if (layout.valid) {
                    EnsureKtp400HardwareInfoSeed();
                    PersistKtp400HardwareInfoSeed();
                    return;
                }
            }
            std::memset(data_.data(), 0, data_.size());
        }
    }
    const uint32_t part_lba    = 1u;
    const uint32_t part_blocks = total_blocks - part_lba;
    uint8_t* mbr = data_.data();
    mbr[446 + 0] = 0x00;
    mbr[446 + 1] = 0x01;
    mbr[446 + 2] = 0x01;
    mbr[446 + 3] = 0x00;
    mbr[446 + 4] = 0x0B;            /* KTP400 registry maps 0b -> PDCFS on eMMC */
    mbr[446 + 5] = 0xFE;
    mbr[446 + 6] = 0xFF;
    mbr[446 + 7] = 0xFF;
    Put32(mbr + 446 + 8, part_lba);
    Put32(mbr + 446 + 12, part_blocks);
    mbr[510] = 0x55; mbr[511] = 0xAA;
    PersistBackingRange(0u, 512u);   /* mspart consumes LBA0 before PDCFS. */
    EnsureKtp400HardwareInfoSeed();
    EnsureKtp400PdcfsRootDirs();
    PersistKtp400HardwareInfoSeed();
    FlushDirtyBackingRanges();
}

void SdCardMedia::EnsureKtp400PdcfsRootDirs() {
    ktp400_emmc::EnsurePdcfsLayout(
        data_, [this](uint64_t offset, uint64_t length) {
            PersistBackingRange(offset, length);
        });
}

void SdCardMedia::EnsureKtp400HardwareInfoSeed() {
    ktp400_emmc::EnsureFactoryLayout(
        data_, fwf_info_stream_, fwf_info_fallback_object_,
        hardware_mac_);
}

void SdCardMedia::PersistBackingRange(uint64_t offset, uint64_t length) {
    if (!persistent_ || offset >= data_.size() || length == 0u)
        return;

    const uint64_t end = std::min<uint64_t>(offset + length, data_.size());
    if (end <= offset)
        return;

    MarkDirtyBackingRange(offset, end - offset);
    if (dirty_bytes_pending_ < (4ull * 1024ull * 1024ull))
        return;

    FlushDirtyBackingRanges();
}

void SdCardMedia::MarkDirtyBackingRange(uint64_t offset, uint64_t length) {
    if (offset >= data_.size() || length == 0u)
        return;

    uint64_t begin = offset;
    uint64_t end = std::min<uint64_t>(offset + length, data_.size());
    if (end <= begin)
        return;

    const uint64_t old_span = end - begin;
    for (auto it = dirty_ranges_.begin(); it != dirty_ranges_.end();) {
        if (end < it->first || begin > it->second) {
            ++it;
            continue;
        }
        begin = std::min(begin, it->first);
        end   = std::max(end,   it->second);
        it = dirty_ranges_.erase(it);
    }
    dirty_ranges_.emplace_back(begin, end);
    dirty_bytes_pending_ += old_span;
}

void SdCardMedia::FlushDirtyBackingRanges() {
    if (dirty_ranges_.empty() || data_.empty())
        return;

    std::fstream out(kKtp400PdcfsBacking, std::ios::binary | std::ios::in | std::ios::out);
    if (!out.good()) {
        std::ofstream create(kKtp400PdcfsBacking, std::ios::binary | std::ios::trunc);
        if (!data_.empty()) {
            create.seekp(static_cast<std::streamoff>(data_.size() - 1u));
            const char zero = 0;
            create.write(&zero, 1);
        }
        create.close();
        out.open(kKtp400PdcfsBacking, std::ios::binary | std::ios::in | std::ios::out);
    }
    if (!out.good())
        return;

    std::sort(dirty_ranges_.begin(), dirty_ranges_.end());
    for (const auto& r : dirty_ranges_) {
        const uint64_t begin = std::min<uint64_t>(r.first, data_.size());
        const uint64_t end   = std::min<uint64_t>(r.second, data_.size());
        if (end <= begin)
            continue;
        out.seekp(static_cast<std::streamoff>(begin));
        out.write(reinterpret_cast<const char*>(data_.data() + begin),
                  static_cast<std::streamsize>(end - begin));
    }
    dirty_ranges_.clear();
    dirty_bytes_pending_ = 0;
}

bool SdCardMedia::OverlapsKtp400FactoryArea(uint64_t offset, uint64_t length) const {
    if (length == 0u)
        return false;
    const uint64_t end = offset + length;
    const auto overlaps = [&](uint64_t begin, uint64_t len) {
        const uint64_t r_end = begin + len;
        return offset < r_end && end > begin;
    };

    /* Siemens factory/HWF/FWF media areas seeded by EnsureKtp400HardwareInfoSeed().
       They are not ordinary PDCFS user data; a real panel keeps these outside
       the mutable \flash filesystem. */
    return overlaps(kKtp400FactoryTableOff, 0x00006000ull + kKtp400PartLbaBytes) ||
           overlaps(kKtp400FwfInfoOff, kKtp400FwfInfoSize) ||
           overlaps(kKtp400FwfInfoOff + kKtp400PartLbaBytes, kKtp400FwfInfoSize);
}

void SdCardMedia::PersistKtp400HardwareInfoSeed() {
    /* The Siemens factory/HWF sector table is consumed through more than one
       path: BSPIO reads it from the live card, while CFWFInfo/place_sft later
       re-opens the SSD and expects the same bytes to be physically present.
       Keeping it only in RAM lets early BSPIO succeed but leaves
       CFWFInfo_internal::LoadFWFInfoFromSSD with sizeOfHwf == 0. */
    PersistBackingRange(kKtp400FactoryTableOff,
                        0x00006000u + kKtp400PartLbaBytes);

    size_t object_len = 0;
    if (!fwf_info_stream_.empty() && fwf_info_stream_.size() >= 8u) {
        object_len = static_cast<size_t>(Get32(fwf_info_stream_.data())) + 8u;
        if (object_len < 8u || object_len > fwf_info_stream_.size())
            object_len = fwf_info_stream_.size();
    } else if (!fwf_info_fallback_object_.empty()) {
        object_len = fwf_info_fallback_object_.size() + 9u;
    }
    object_len = std::min<size_t>(object_len, kKtp400FwfInfoSize);
    if (object_len != 0) {
        PersistBackingRange(kKtp400FwfInfoOff,
                            static_cast<uint64_t>(object_len) + kKtp400PartLbaBytes);
    }

    if (!fwf_info_stream_.empty()) {
        PersistBackingRange(kKtp400FwfInfoOff + kKtp400FwfPayload0Off,
                            static_cast<uint64_t>(kKtp400FwfPayload0Len) +
                            kKtp400PartLbaBytes);
        PersistBackingRange(kKtp400FwfInfoOff + kKtp400FwfPayload1Off,
                            static_cast<uint64_t>(kKtp400FwfPayload1Len) +
                            kKtp400PartLbaBytes);
    }
}

void SdCardMedia::Erase(uint64_t first_address, uint64_t last_address) {
    const uint64_t lo = std::min(first_address, last_address);
    const uint64_t hi = std::max(first_address, last_address);
    if (lo >= data_.size()) return;
    const uint64_t first = lo & ~511ull;
    const uint64_t last =
        std::min<uint64_t>((hi & ~511ull) + 512ull, data_.size());
    std::memset(data_.data() + first, 0, static_cast<size_t>(last - first));
    if (OverlapsKtp400FactoryArea(first, last - first)) {
        EnsureKtp400HardwareInfoSeed();
        PersistKtp400HardwareInfoSeed();
    }
    PersistBackingRange(first, last - first);
    FlushDirtyBackingRanges();
}

void SdCardMedia::Read(uint64_t offset, uint8_t* dst512) const {
    if (offset + 512u <= data_.size())
        std::memcpy(dst512, data_.data() + offset, 512u);
    else
        std::memset(dst512, 0, 512u);
}

void SdCardMedia::Write(uint64_t offset, const uint8_t* src512) {
    if (offset + 512u > data_.size()) return;
    std::memcpy(data_.data() + offset, src512, 512u);
    if (persistent_) {
        if (OverlapsKtp400FactoryArea(offset, 512u)) {
            EnsureKtp400HardwareInfoSeed();
            PersistKtp400HardwareInfoSeed();
        }
        PersistBackingRange(offset, 512u);
    }
}

void SdCardMedia::Commit() {
    FlushDirtyBackingRanges();
}
