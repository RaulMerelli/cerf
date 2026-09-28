#include "ktp_mobile_sd_card_backend.h"

#include "../../boot/fwf_oms_reader.h"
#include "../../core/log.h"
#include "ktp_mobile_factory_layout.h"
#include "ktp_mobile_fwf_fsf_container.h"
#include "ktp_mobile_pdcfs_layout.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {

constexpr uint32_t kFactoryTableOffset = 0x00101000u;
constexpr uint32_t kPartitionLbaBytes = 0x00000200u;
constexpr uint32_t kFwfInfoOffset = 0x02000000u;

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

bool IsValidBacking(const std::vector<uint8_t>& data) {
    if (data.size() < 4096u || data[510] != 0x55u || data[511] != 0xAAu)
        return false;
    const uint8_t part_type = data[450u];
    const uint32_t part_lba = Get32(data.data() + 454u);
    const uint32_t part_blocks = Get32(data.data() + 458u);
    if ((part_type != 0x0Bu && part_type != 0x0Cu) || part_lba == 0u ||
        part_blocks == 0u || uint64_t(part_lba) + part_blocks > data.size() / 512u)
        return false;
    const uint64_t bpb_offset = uint64_t(part_lba) * 512u;
    const uint8_t* bpb = data.data() + bpb_offset;
    const uint16_t bytes_per_sector = uint16_t(bpb[11] | (uint16_t(bpb[12]) << 8u));
    return bytes_per_sector == 512u && bpb[13] != 0u && bpb[16] != 0u &&
           Get32(bpb + 36u) != 0u && Get32(bpb + 44u) >= 2u &&
           bpb[510] == 0x55u && bpb[511] == 0xAAu;
}

}

KtpMobileSdCardBackend::KtpMobileSdCardBackend(
    std::string backing_path, std::string device_dir, std::string container_name,
    KtpMobileOpType op_type, KtpMobilePanel panel, std::array<uint8_t, 6> mac)
    : backing_path_(std::move(backing_path)),
      fwf_container_(ktp_mobile_emmc::LoadFwfContainer(device_dir, container_name)),
      op_type_(op_type), panel_(panel), hardware_mac_(mac) {}


void KtpMobileSdCardBackend::Initialize(std::vector<uint8_t>& data) {
    if (data.size() < 4096u) return;
    std::memset(data.data(), 0, data.size());
    std::ifstream in(backing_path_, std::ios::binary);
    if (in.good()) {
        in.read(reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        if (static_cast<size_t>(in.gcount()) == data.size() && IsValidBacking(data)) {
            EnsureHardwareInfo(data);
            PersistHardwareInfo(data);
            return;
        }
        std::memset(data.data(), 0, data.size());
    }

    const uint32_t total_blocks = static_cast<uint32_t>(data.size() / 512u);
    const uint32_t part_lba = 1u;
    const uint32_t part_blocks = total_blocks - part_lba;
    uint8_t* mbr = data.data();
    mbr[446] = 0x00u;
    mbr[447] = 0x01u;
    mbr[448] = 0x01u;
    mbr[449] = 0x00u;
    mbr[450] = 0x0Bu;
    mbr[451] = 0xFEu;
    mbr[452] = 0xFFu;
    mbr[453] = 0xFFu;
    Put32(mbr + 454u, part_lba);
    Put32(mbr + 458u, part_blocks);
    mbr[510] = 0x55u;
    mbr[511] = 0xAAu;
    Persist(data, 0u, 512u);
    EnsureHardwareInfo(data);
    ktp_mobile_emmc::EnsurePdcfsLayout(
        data, [this, &data](uint64_t offset, uint64_t length) {
            Persist(data, offset, length);
        }, ktp_mobile_fwf::ParseFsfVolume(fwf_container_));
    PersistHardwareInfo(data);
    Flush(data);
}

void KtpMobileSdCardBackend::EnsureHardwareInfo(std::vector<uint8_t>& data) {
    ktp_mobile_emmc::EnsureFactoryLayout(
        data, fwf_container_, hardware_mac_, op_type_, panel_);
}

void KtpMobileSdCardBackend::PersistHardwareInfo(
    const std::vector<uint8_t>& data) {
    Persist(data, kFactoryTableOffset, 0x00006000u + kPartitionLbaBytes);
    std::vector<uint8_t> installed_firmware;
    if (cerf::fwf_oms::ExtractInstalledFirmwareSummary(
            fwf_container_.data(), fwf_container_.size(), installed_firmware)) {
        Persist(data, kFwfInfoOffset,
                static_cast<uint64_t>(installed_firmware.size()) + 8u +
                    kPartitionLbaBytes);
    }
}

void KtpMobileSdCardBackend::Persist(const std::vector<uint8_t>& data,
                                     uint64_t offset, uint64_t length) {
    if (offset >= data.size() || length == 0u) return;
    MarkDirty(offset, length, data.size());
    if (dirty_bytes_pending_ >= 4ull * 1024ull * 1024ull) Flush(data);
}

void KtpMobileSdCardBackend::MarkDirty(uint64_t offset, uint64_t length,
                                       uint64_t size) {
    uint64_t begin = offset;
    uint64_t end = std::min<uint64_t>(offset + length, size);
    if (end <= begin) return;
    const uint64_t span = end - begin;
    for (auto it = dirty_ranges_.begin(); it != dirty_ranges_.end();) {
        if (end < it->first || begin > it->second) {
            ++it;
            continue;
        }
        begin = std::min(begin, it->first);
        end = std::max(end, it->second);
        it = dirty_ranges_.erase(it);
    }
    dirty_ranges_.emplace_back(begin, end);
    dirty_bytes_pending_ += span;
}

void KtpMobileSdCardBackend::Flush(const std::vector<uint8_t>& data) {
    if (dirty_ranges_.empty() || data.empty()) return;
    std::fstream out(backing_path_, std::ios::binary | std::ios::in |
                                        std::ios::out);
    if (!out.good()) {
        std::ofstream create(backing_path_, std::ios::binary | std::ios::trunc);
        create.seekp(static_cast<std::streamoff>(data.size() - 1u));
        const char zero = 0;
        create.write(&zero, 1);
        create.close();
        out.open(backing_path_, std::ios::binary | std::ios::in |
                                    std::ios::out);
    }
    if (!out.good()) {
        LOG(Caution, "KTP Mobile SD card: cannot open the backing file %s\n", backing_path_.c_str());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::sort(dirty_ranges_.begin(), dirty_ranges_.end());
    for (const auto& range : dirty_ranges_) {
        const uint64_t begin = std::min<uint64_t>(range.first, data.size());
        const uint64_t end = std::min<uint64_t>(range.second, data.size());
        if (end <= begin) continue;
        out.seekp(static_cast<std::streamoff>(begin));
        out.write(reinterpret_cast<const char*>(data.data() + begin),
                  static_cast<std::streamsize>(end - begin));
        if (!out.good()) {
            LOG(Caution, "KTP Mobile SD card: write of 0x%llX bytes at 0x%llX to %s failed\n",
                static_cast<unsigned long long>(end - begin), static_cast<unsigned long long>(begin),
                backing_path_.c_str());
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
    dirty_ranges_.clear();
    dirty_bytes_pending_ = 0u;
}
