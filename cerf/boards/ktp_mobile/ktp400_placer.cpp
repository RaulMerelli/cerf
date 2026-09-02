#include "ktp_mobile_placer.h"

namespace {
class Ktp400FPlacer final : public KtpMobilePlacer<Board::HmiKtp400FMobile, KtpMobileOpType::Ktp400F, 480u, 272u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;

};
} // namespace
REGISTER_SERVICE_AS(Ktp400FPlacer, BoardBootPlacer);
