#include "ktp_mobile_context.h"

namespace {
class Ktp900Context final : public KtpMobileContext<Board::HmiKtp900Mobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp900Context, BoardContext);
