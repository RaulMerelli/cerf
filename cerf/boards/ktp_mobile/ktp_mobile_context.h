#pragma once

#include "../board_context.h"
#include "../../core/cerf_emulator.h"

template <Board kBoard, uint32_t kWidth, uint32_t kHeight> class KtpMobileContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    Board GetBoard() const override { return kBoard; }
    SocFamily GetSoc() const override { return SocFamily::iMX6; }
    CpuArch GetCpuArch() const override { return CpuArch::Arm; }
    RomPlacingMode GetRomPlacingMode() const override { return RomPlacingMode::FlatContainer; }

    std::optional<PreferredWindowSize> GetPreferredWindowSize() const override {
        return PreferredWindowSize{kWidth, kHeight};
    }

};
