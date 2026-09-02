#include "ktp_mobile_placer.h"

namespace {
class Ktp700FArcticPlacer final
    : public KtpMobilePlacer<Board::HmiKtp700FArcticMobile, KtpMobileOpType::Ktp700FArctic, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;

};
} // namespace
REGISTER_SERVICE_AS(Ktp700FArcticPlacer, BoardBootPlacer);
