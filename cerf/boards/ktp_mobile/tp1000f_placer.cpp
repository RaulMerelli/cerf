#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Tp1000fPlacer final : public KtpMobilePlacer<BoardId::HmiTp1000fMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Tp1000fPlacer, BoardBootPlacer);
