#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp900FPlacer final : public KtpMobilePlacer<BoardId::HmiKtp900FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp900FPlacer, BoardBootPlacer);
