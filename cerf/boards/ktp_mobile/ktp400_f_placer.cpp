#include "ktp_mobile_placer.h"

namespace {
class Ktp400FPlacer final : public KtpMobilePlacer<Board::HmiKtp400FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp400FPlacer, BoardBootPlacer);
