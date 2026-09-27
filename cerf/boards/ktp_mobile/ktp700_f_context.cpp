#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700FContext final : public KtpMobileContext<BoardId::HmiKtp700FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FContext, BoardContext);
