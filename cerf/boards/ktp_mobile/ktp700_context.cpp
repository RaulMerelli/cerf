#include "ktp_mobile_context.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700Context final : public KtpMobileContext<BoardId::HmiKtp700Mobile> {
public:
    using KtpMobileContext::KtpMobileContext;
};
}

REGISTER_SERVICE_AS(Ktp700Context, BoardContext);
