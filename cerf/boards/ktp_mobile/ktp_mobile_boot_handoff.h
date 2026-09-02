#pragma once

#include "../../core/service.h"
#include "../../peripherals/sd_card/ktp_mobile_hardware_info.h"

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
    void OnAllReady() override;

    void Place(const KtpMobileOalLayout& oal);

private:
    void CompleteWatchdogBootHandoff();
};
