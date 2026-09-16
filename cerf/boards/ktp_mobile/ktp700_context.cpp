#include "ktp_mobile_context.h"

namespace {
class Ktp700Context final : public KtpMobileContext<Board::HmiKtp700Mobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700Context, BoardContext);
