#include "ktp_mobile_context.h"

namespace {
class Ktp400FContext final : public KtpMobileContext<Board::HmiKtp400FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp400FContext, BoardContext);
