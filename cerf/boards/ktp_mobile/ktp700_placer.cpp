#include "ktp_mobile_placer.h"

namespace {
class Ktp700Placer final : public KtpMobilePlacer<Board::HmiKtp700Mobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700Placer, BoardBootPlacer);
