#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp700Placer final : public KtpMobilePlacer<BoardId::HmiKtp700Mobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp700Placer, BoardBootPlacer);
