#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp400FContext final : public KtpMobileContext<BoardId::HmiKtp400FMobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp400FContext, BoardContext);
