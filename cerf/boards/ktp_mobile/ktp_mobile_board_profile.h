#pragma once

#include "../board_context.h"
#include "ktp_mobile_hardware_info.h"

struct KtpMobileBoardProfile {
    Board board;
    KtpMobileOpType op_type;
    KtpMobilePanel panel;
    const char* touch_size_suffix;
};

const KtpMobileBoardProfile& KtpMobileBoardProfileFor(Board board);
