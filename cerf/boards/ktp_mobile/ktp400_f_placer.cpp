#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp400FPlacer final : public KtpMobilePlacer<BoardId::HmiKtp400FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp400FPlacer, BoardBootPlacer);
