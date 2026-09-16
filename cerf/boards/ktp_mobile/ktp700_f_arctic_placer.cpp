#include "ktp_mobile_placer.h"

namespace {
class Ktp700FArcticPlacer final : public KtpMobilePlacer<Board::HmiKtp700FArcticMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700FArcticPlacer, BoardBootPlacer);
