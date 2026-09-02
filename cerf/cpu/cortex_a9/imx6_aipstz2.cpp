#include "imx6_aipstz.h"
namespace {
class Imx6Aipstz2 final : public Imx6Aipstz<0x0217C000u> {
public:
    using Imx6Aipstz::Imx6Aipstz;

};
} // namespace
REGISTER_SERVICE(Imx6Aipstz2);
