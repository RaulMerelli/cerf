#include "ktp_mobile_emmc.h"

#include "../../core/cerf_emulator.h"
#include "../../core/cerf_paths.h"
#include "../../core/device_config.h"
#include "../../core/fatal.h"
#include "../../net/network_backend.h"
#include "../../socs/imx6/imx6_fec.h"
#include "../board_context.h"
#include "ktp_mobile_board_profile.h"
#include "ktp_mobile_id.h"
#include "ktp_mobile_sd_card_backend.h"

#include <cstring>

namespace {

constexpr uint32_t kSlotIndex   = 3u;
constexpr uint32_t kBlockBytes  = 512u;
constexpr uint32_t kMediaBytes  = 128u * 1024u * 1024u;
constexpr uint32_t kSectorCount = kMediaBytes / kBlockBytes;

constexpr SdCardCid kCid = {
    0x03u, 'C', 'E', 'C', 'E', 'R', 'F', '0',
    0x10u, 0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0x40u, 0x00u,
};

constexpr EmmcCsdFields kCsd = {
    .csd_structure  = 3u,
    .spec_vers      = 4u,
    .taac           = 0x0Eu,
    .nsac           = 0x00u,
    .tran_speed     = 0x32u,
    .ccc            = 0x5B5u,
    .read_bl_len    = 9u,
    .c_size         = 0xFFFu,
    .vdd_r_curr_min = 0u,
    .vdd_r_curr_max = 0u,
    .vdd_w_curr_min = 0u,
    .vdd_w_curr_max = 0u,
    .c_size_mult    = 4u,
    .erase_grp_size = 0u,
    .erase_grp_mult = 0u,
    .wp_grp_size    = 0u,
    .wp_grp_enable  = 0u,
    .r2w_factor     = 0u,
    .write_bl_len   = 9u,
};

constexpr EmmcExtCsdByte kExtCsdProperties[] = {
    {160u, 0x07u}, {179u, 0x48u}, {192u, 0x08u}, {194u, 0x02u},
    {196u, 0x03u}, {197u, 0x01u}, {199u, 0x01u}, {221u, 0x01u},
    {222u, 0x01u}, {223u, 0x01u}, {224u, 0x01u}, {225u, 0x01u},
    {226u, 0x20u}, {228u, 0x07u},
};

}

KtpMobileEmmc::~KtpMobileEmmc() {
    if (backing_) backing_->Flush(data_);
}

bool KtpMobileEmmc::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && BoardId::IsKtpMobile(board->GetBoardId());
}

void KtpMobileEmmc::OnReady() {
    const auto& config = emu_.Get<DeviceConfig>();
    const auto& profile = KtpMobileBoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
    data_.assign(kMediaBytes, 0u);
    backing_ = std::make_unique<KtpMobileSdCardBackend>(
        ResolveDeviceFile(config.device_name, config.storage_emmc),
        GetDeviceDir(config.device_name), config.rom_primary, profile.op_type, profile.panel,
        emu_.Get<NetworkBackend>().MacForReceiver(
            kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet));
    backing_->Initialize(data_);
    EmmcCardBase::OnReady();
}

uint32_t KtpMobileEmmc::SlotIndex() const { return kSlotIndex; }

SdCardCid KtpMobileEmmc::Cid() const { return kCid; }

EmmcCsdFields KtpMobileEmmc::Csd() const { return kCsd; }

std::span<const EmmcExtCsdByte> KtpMobileEmmc::ExtCsdProperties() const {
    return kExtCsdProperties;
}

uint32_t KtpMobileEmmc::SectorCount() const { return kSectorCount; }

uint8_t KtpMobileEmmc::ErasedMemCont() const {
    return cerf_mmc::kErasedMemContZeros;
}

void KtpMobileEmmc::ReadBlock(uint32_t sector, uint8_t* out) {
    RequireSector(sector, "read");
    std::memcpy(out, data_.data() + static_cast<size_t>(sector) * kBlockBytes, kBlockBytes);
}

void KtpMobileEmmc::WriteBlock(uint32_t sector, const uint8_t* data) {
    RequireSector(sector, "write");
    const uint64_t offset = static_cast<uint64_t>(sector) * kBlockBytes;
    std::memcpy(data_.data() + static_cast<size_t>(offset), data, kBlockBytes);
    backing_->Persist(data_, offset, kBlockBytes);
}

void KtpMobileEmmc::RequireSector(uint32_t sector, const char* what) const {
    if (sector >= kSectorCount)
        emu_.Get<Fatal>().Die("KTP Mobile eMMC: %s of sector %u past the %u-sector card",
                              what, sector, kSectorCount);
}

REGISTER_SERVICE_AS(KtpMobileEmmc, MmcCard);
