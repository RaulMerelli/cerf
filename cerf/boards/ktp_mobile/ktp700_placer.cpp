#include "ktp_mobile_placer.h"

namespace {
class Ktp700Placer final : public KtpMobilePlacer<Board::HmiKtp700Mobile, KtpMobileOpType::Ktp700, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;

};
class Ktp700FPlacer final
    : public KtpMobilePlacer<Board::HmiKtp700FMobile, KtpMobileOpType::Ktp700F, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
class Ktp900Placer final : public KtpMobilePlacer<Board::HmiKtp900Mobile, KtpMobileOpType::Ktp900, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
class Ktp900FPlacer final
    : public KtpMobilePlacer<Board::HmiKtp900FMobile, KtpMobileOpType::Ktp900F, 800u, 480u> {
public:
    using KtpMobilePlacer::KtpMobilePlacer;
};
} // namespace
REGISTER_SERVICE_AS(Ktp700Placer, BoardBootPlacer);
REGISTER_SERVICE_AS(Ktp700FPlacer, BoardBootPlacer);
REGISTER_SERVICE_AS(Ktp900Placer, BoardBootPlacer);
REGISTER_SERVICE_AS(Ktp900FPlacer, BoardBootPlacer);
