#pragma once

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "ktp_mobile_board_profile.h"

template <Board kBoard> class KtpMobileContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board GetBoard() const override { return kBoard; }
    SocFamily GetSoc() const override { return SocFamily::iMX6; }
    CpuArch GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        const auto& profile = KtpMobileBoardProfileFor(kBoard);
        return PreferredWindowSize{profile.panel.width, profile.panel.height};
    }

};
