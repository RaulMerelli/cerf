#include "ktp_mobile_placer.h"

namespace {
class Ktp700FHwPlacer final
    : public KtpMobilePlacer<Board::HmiKtp700FHwMobile, KtpMobileOpType::Ktp700FHw, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;

};
} // namespace
REGISTER_SERVICE_AS(Ktp700FHwPlacer, BoardBootPlacer);
