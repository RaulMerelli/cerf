#include "ktp_mobile_context.h"

namespace {
class Tp1000fContext final : public KtpMobileContext<Board::HmiTp1000fMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Tp1000fContext, BoardContext);
