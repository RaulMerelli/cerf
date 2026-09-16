#include "ktp_mobile_placer.h"

namespace {
class Ktp900Placer final : public KtpMobilePlacer<Board::HmiKtp900Mobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp900Placer, BoardBootPlacer);
