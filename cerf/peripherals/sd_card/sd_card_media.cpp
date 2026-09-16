#include "sd_card_media.h"

#include <algorithm>
#include <cstring>

namespace {

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

} // namespace

SdCardMedia::SdCardMedia(uint64_t size_bytes) {
    const uint64_t blocks = (size_bytes + 511u) / 512u;
    data_.assign(static_cast<size_t>(blocks * 512u), 0u);
    BuildMinimalFat16();
}

SdCardMedia::~SdCardMedia() {
    if (backend_) backend_->Flush(data_);
}

void SdCardMedia::Configure(std::unique_ptr<SdCardMediaBackend> backend) {
    backend_ = std::move(backend);
}

void SdCardMedia::InitializeMmcLayout() {
    if (layout_initialized_ || !backend_) return;
    layout_initialized_ = true;
    backend_->Initialize(data_);
}

void SdCardMedia::BuildMinimalFat16() {
    const uint32_t total_blocks = static_cast<uint32_t>(data_.size() / 512u);
    if (total_blocks < 4096u) return;

    const uint32_t part_lba = 1u;
    const uint32_t part_blocks = total_blocks - part_lba;
    uint8_t* mbr = data_.data();
    std::memset(mbr, 0, 512u);
    mbr[446 + 0] = 0x00;
    mbr[446 + 1] = 0x01;
    mbr[446 + 2] = 0x01;
    mbr[446 + 3] = 0x00;
    mbr[446 + 4] = 0x06;
    mbr[446 + 5] = 0xFE;
    mbr[446 + 6] = 0xFF;
    mbr[446 + 7] = 0xFF;
    Put32(mbr + 446 + 8, part_lba);
    Put32(mbr + 446 + 12, part_blocks);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    uint8_t* bpb = data_.data() + part_lba * 512u;
    std::memset(bpb, 0, 512u);
    bpb[0] = 0xEB;
    bpb[1] = 0x3C;
    bpb[2] = 0x90;
    std::memcpy(bpb + 3, "CERFSD  ", 8);
    Put16(bpb + 11, 512);
    bpb[13] = 2;
    Put16(bpb + 14, 1);
    bpb[16] = 2;
    Put16(bpb + 17, 512);
    Put16(bpb + 19, part_blocks < 65536u ? static_cast<uint16_t>(part_blocks) : 0u);
    bpb[21] = 0xF8;
    Put16(bpb + 22, 32);
    Put16(bpb + 24, 63);
    Put16(bpb + 26, 255);
    Put32(bpb + 28, part_lba);
    Put32(bpb + 32, part_blocks >= 65536u ? part_blocks : 0u);
    bpb[36] = 0x80;
    bpb[38] = 0x29;
    Put32(bpb + 39, 0x43455246u); /* volume serial: "FREC" LE */
    std::memcpy(bpb + 43, "CERFSD     ", 11);
    std::memcpy(bpb + 54, "FAT16   ", 8);
    bpb[510] = 0x55;
    bpb[511] = 0xAA;

    uint8_t* fat0 = data_.data() + (part_lba + 1u) * 512u;
    uint8_t* fat1 = data_.data() + (part_lba + 33u) * 512u;
    std::memset(fat0, 0, 32u * 512u);
    std::memset(fat1, 0, 32u * 512u);
    fat0[0] = 0xF8;
    fat0[1] = 0xFF;
    fat0[2] = 0xFF;
    fat0[3] = 0xFF;
    fat1[0] = 0xF8;
    fat1[1] = 0xFF;
    fat1[2] = 0xFF;
    fat1[3] = 0xFF;
}

void SdCardMedia::Erase(uint64_t first_address, uint64_t last_address) {
    const uint64_t lo = std::min(first_address, last_address);
    const uint64_t hi = std::max(first_address, last_address);
    if (lo >= data_.size()) return;
    const uint64_t first = lo & ~511ull;
    const uint64_t last = std::min<uint64_t>((hi & ~511ull) + 512ull, data_.size());
    std::memset(data_.data() + first, 0, static_cast<size_t>(last - first));
    if (persistent_) {
        backend_->Persist(data_, first, last - first);
        backend_->Flush(data_);
    }
}

bool SdCardMedia::Read(uint64_t offset, uint8_t* dst512) const {
    if (offset + 512u > data_.size()) {
        std::memset(dst512, 0, 512u);
        return false;
    }
    std::memcpy(dst512, data_.data() + offset, 512u);
    return true;
}

bool SdCardMedia::Write(uint64_t offset, const uint8_t* src512) {
    if (offset + 512u > data_.size()) return false;
    std::memcpy(data_.data() + offset, src512, 512u);
    if (persistent_) {
        backend_->Persist(data_, offset, 512u);
    }
    return true;
}

void SdCardMedia::Commit() {
    if (backend_) backend_->Flush(data_);
}
