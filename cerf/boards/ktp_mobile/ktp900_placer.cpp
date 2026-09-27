#include "ktp_mobile_placer.h"
#include "ktp_mobile_id.h"

namespace {
class Ktp900Placer final : public KtpMobilePlacer<BoardId::HmiKtp900Mobile> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
}

REGISTER_SERVICE_AS(Ktp900Placer, BoardBootPlacer);
