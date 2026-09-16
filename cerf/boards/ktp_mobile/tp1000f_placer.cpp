#include "ktp_mobile_placer.h"

namespace {
class Tp1000fPlacer final : public KtpMobilePlacer<Board::HmiTp1000fMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Tp1000fPlacer, BoardBootPlacer);
