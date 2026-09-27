#pragma once

#include <string_view>
#include "ktp_mobile_hardware_info.h"

struct KtpMobileBoardProfile {
    std::string_view board;
    KtpMobileOpType op_type;
    KtpMobilePanel panel;
    const char* touch_size_suffix;
    bool has_f_module;
};

const KtpMobileBoardProfile* TryKtpMobileBoardProfileFor(std::string_view board);
const KtpMobileBoardProfile& KtpMobileBoardProfileFor(std::string_view board);
