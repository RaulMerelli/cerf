#include "ktp_mobile_placer.h"

namespace {
class Ktp700FHwPlacer final : public KtpMobilePlacer<Board::HmiKtp700FHwMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700FHwPlacer, BoardBootPlacer);
