#include "ktp_mobile_placer.h"

namespace {
class Tp1000fPlacer final : public KtpMobilePlacer<Board::HmiTp1000fMobile, KtpMobileOpType::Tp1000F, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;

};
} // namespace
REGISTER_SERVICE_AS(Tp1000fPlacer, BoardBootPlacer);
