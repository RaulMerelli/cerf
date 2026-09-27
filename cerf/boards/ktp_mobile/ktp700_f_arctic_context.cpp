#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700FArcticContext final : public KtpMobileContext<BoardId::HmiKtp700FArcticMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FArcticContext, BoardContext);
