#pragma once

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "ktp_mobile_board_profile.h"
#include "../../socs/imx6/imx6_id.h"

template <const std::string_view& kBoard> class KtpMobileContext : public BoardContext {
public:
    using BoardContext::BoardContext;

    std::string_view GetBoardId() const override { return kBoard; }
};
