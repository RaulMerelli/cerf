#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Tp1000fContext final : public KtpMobileContext<BoardId::HmiTp1000fMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Tp1000fContext, BoardContext);
