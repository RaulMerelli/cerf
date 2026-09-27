#include "../../peripherals/sd_card/sd_card_configuration.h"

#include "../../core/cerf_emulator.h"
#include "../../core/cerf_paths.h"
#include "../../core/device_config.h"
#include "../../net/network_backend.h"
#include "../../peripherals/sd_card/sd_card.h"
#include "../../socs/imx6/imx6_fec.h"
#include "../board_context.h"
#include "ktp_mobile_board_profile.h"
#include "ktp_mobile_sd_card_backend.h"

#include <memory>
#include "ktp_mobile_id.h"

namespace {

class KtpMobileSdCardConfiguration final : public SdCardConfiguration {
public:
    using SdCardConfiguration::SdCardConfiguration;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardId::IsKtpMobile(board->GetBoardId());
    }

    uint64_t MediaSizeBytes() const override { return 128ull * 1024u * 1024u; }

    void Configure(SdCard& card) override {
        const auto& config = emu_.Get<DeviceConfig>();
        const auto& profile = KtpMobileBoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
        card.ConfigureMedia(std::make_unique<KtpMobileSdCardBackend>(
            GetDeviceDir(config.device_name), config.rom_primary,
            profile.op_type,
            emu_.Get<NetworkBackend>().MacForReceiver(
                kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet)));
    }
};

}

REGISTER_SERVICE_AS(KtpMobileSdCardConfiguration, SdCardConfiguration);
