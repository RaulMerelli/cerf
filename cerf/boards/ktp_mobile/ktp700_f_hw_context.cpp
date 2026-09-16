#include "ktp_mobile_context.h"

namespace {
class Ktp700FHwContext final : public KtpMobileContext<Board::HmiKtp700FHwMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FHwContext, BoardContext);
