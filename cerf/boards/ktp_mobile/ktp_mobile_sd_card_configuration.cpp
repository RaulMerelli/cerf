#include "../../peripherals/sd_card/sd_card_configuration.h"

#include "../../core/cerf_emulator.h"
#include "../../core/cerf_paths.h"
#include "../../core/device_config.h"
#include "../../net/network_backend.h"
#include "../../peripherals/sd_card/sd_card.h"
#include "../board_context.h"

namespace {

class KtpMobileSdCardConfiguration final : public SdCardConfiguration {
public:
    using SdCardConfiguration::SdCardConfiguration;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && BoardContext::IsKtpMobile(board->GetBoard());
    }

    void Configure(SdCard& card) override {
        const auto& config = emu_.Get<DeviceConfig>();
        auto* board = emu_.TryGet<BoardContext>();
        KtpMobileOpType op_type = KtpMobileOpType::Ktp400F;
        KtpMobilePanel panel{480u, 272u};
        if (board) {
            switch (board->GetBoard()) {
            case Board::HmiKtp700Mobile:
                op_type = KtpMobileOpType::Ktp700;
                panel = {800u, 480u};
                break;
            case Board::HmiKtp700FMobile:
                op_type = KtpMobileOpType::Ktp700F;
                panel = {800u, 480u};
                break;
            case Board::HmiKtp900Mobile:
                op_type = KtpMobileOpType::Ktp900;
                panel = {800u, 480u};
                break;
            case Board::HmiKtp900FMobile:
                op_type = KtpMobileOpType::Ktp900F;
                panel = {800u, 480u};
                break;
            case Board::HmiKtp700FHwMobile:
                op_type = KtpMobileOpType::Ktp700FHw;
                panel = {800u, 480u};
                break;
            case Board::HmiKtp700FArcticMobile:
                op_type = KtpMobileOpType::Ktp700FArctic;
                panel = {800u, 480u};
                break;
            case Board::HmiTp1000fMobile:
                op_type = KtpMobileOpType::Tp1000F;
                panel = {800u, 480u};
                break;
            default: break;
            }
        }
        card.ConfigureKtp400(GetDeviceDir(config.device_name), config.rom_primary, op_type, panel,
                             emu_.Get<NetworkBackend>().GuestMacAddress());
    }
};

} // namespace

REGISTER_SERVICE_AS(KtpMobileSdCardConfiguration, SdCardConfiguration);
