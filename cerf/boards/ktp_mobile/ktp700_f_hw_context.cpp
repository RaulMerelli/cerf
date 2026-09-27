#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700FHwContext final : public KtpMobileContext<BoardId::HmiKtp700FHwMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FHwContext, BoardContext);
