#include "ktp_mobile_context.h"

namespace {
class Ktp700FArcticContext final : public KtpMobileContext<Board::HmiKtp700FArcticMobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;

};
} // namespace
REGISTER_SERVICE_AS(Ktp700FArcticContext, BoardContext);
