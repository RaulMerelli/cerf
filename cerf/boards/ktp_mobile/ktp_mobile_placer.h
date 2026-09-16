#pragma once

#include "../../boot/board_boot_placer.h"
#include "../../boot/guest_cold_boot.h"
#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "ktp_mobile_board_profile.h"
#include "ktp_mobile_boot_handoff.h"

template <Board kBoard>
class KtpMobilePlacer : public BoardBootPlacer {
public:
    using BoardBootPlacer::BoardBootPlacer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == kBoard;
    }

    void OnReady() override {
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { PlaceAfterRom(); });
    }

    void PlaceAfterRom() override {
        const auto& profile = KtpMobileBoardProfileFor(kBoard);
        const KtpMobileOalLayout oal = {"KtpMobileBootPlacer", profile.op_type, profile.panel};
        emu_.Get<KtpMobileBootHandoff>().Place(oal);
    }
};
