#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp900FContext final : public KtpMobileContext<BoardId::HmiKtp900FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp900FContext, BoardContext);
