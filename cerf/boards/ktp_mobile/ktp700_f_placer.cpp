#include "ktp_mobile_placer.h"

namespace {
class Ktp700FPlacer final : public KtpMobilePlacer<Board::HmiKtp700FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700FPlacer, BoardBootPlacer);
