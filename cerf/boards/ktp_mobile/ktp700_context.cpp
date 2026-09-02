#include "ktp_mobile_context.h"

namespace {
class Ktp700Context final : public KtpMobileContext<Board::HmiKtp700Mobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;

};
class Ktp700FContext final : public KtpMobileContext<Board::HmiKtp700FMobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;
};
class Ktp900Context final : public KtpMobileContext<Board::HmiKtp900Mobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;
};
class Ktp900FContext final : public KtpMobileContext<Board::HmiKtp900FMobile, 800u, 480u> {
public:
    using KtpMobileContext::KtpMobileContext;
};
} // namespace
REGISTER_SERVICE_AS(Ktp700Context, BoardContext);
REGISTER_SERVICE_AS(Ktp700FContext, BoardContext);
REGISTER_SERVICE_AS(Ktp900Context, BoardContext);
REGISTER_SERVICE_AS(Ktp900FContext, BoardContext);
