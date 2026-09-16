#include "ktp_mobile_context.h"

namespace {
class Ktp900FContext final : public KtpMobileContext<Board::HmiKtp900FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp900FContext, BoardContext);
