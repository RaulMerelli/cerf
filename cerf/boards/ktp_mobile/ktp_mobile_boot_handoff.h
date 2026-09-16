#pragma once

#include "../../core/service.h"
#include "ktp_mobile_hardware_info.h"

#include <cstdint>

struct KtpMobileOalLayout {
    const char* log_tag;
    KtpMobileOpType op_type;
    KtpMobilePanel panel;
};

class KtpMobileBootHandoff : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Place(const KtpMobileOalLayout& oal);
};
