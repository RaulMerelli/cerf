#include "ktp_mobile_context.h"

namespace {
class Ktp700FHwContext final : public KtpMobileContext<Board::HmiKtp700FHwMobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;

};
} // namespace
REGISTER_SERVICE_AS(Ktp700FHwContext, BoardContext);
