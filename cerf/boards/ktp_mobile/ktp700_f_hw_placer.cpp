#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700FHwPlacer final : public KtpMobilePlacer<BoardId::HmiKtp700FHwMobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700FHwPlacer, BoardBootPlacer);
