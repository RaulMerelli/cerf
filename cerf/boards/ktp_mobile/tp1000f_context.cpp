#include "ktp_mobile_context.h"

namespace {
class Tp1000fContext final : public KtpMobileContext<Board::HmiTp1000fMobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;

};
} // namespace
REGISTER_SERVICE_AS(Tp1000fContext, BoardContext);
