#include "ktp_mobile_context.h"

namespace {
class Ktp400FContext final : public KtpMobileContext<Board::HmiKtp400FMobile, 480u, 272u> {
public:
    using KtpMobileContext::KtpMobileContext;

};
} // namespace
REGISTER_SERVICE_AS(Ktp400FContext, BoardContext);
