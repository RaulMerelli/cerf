#include "ktp_mobile_placer.h"

namespace {
class Ktp900FPlacer final : public KtpMobilePlacer<Board::HmiKtp900FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp900FPlacer, BoardBootPlacer);
