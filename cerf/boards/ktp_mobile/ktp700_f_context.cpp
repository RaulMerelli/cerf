#include "ktp_mobile_context.h"

namespace {
class Ktp700FContext final : public KtpMobileContext<Board::HmiKtp700FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700FContext, BoardContext);
