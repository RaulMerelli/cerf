#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp900Context final : public KtpMobileContext<BoardId::HmiKtp900Mobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp900Context, BoardContext);
