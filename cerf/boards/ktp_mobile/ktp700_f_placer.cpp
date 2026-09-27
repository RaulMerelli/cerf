#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700FPlacer final : public KtpMobilePlacer<BoardId::HmiKtp700FMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700FPlacer, BoardBootPlacer);
