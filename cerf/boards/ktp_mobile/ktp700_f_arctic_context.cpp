#include "ktp_mobile_context.h"

namespace {
class Ktp700FArcticContext final : public KtpMobileContext<Board::HmiKtp700FArcticMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FArcticContext, BoardContext);
