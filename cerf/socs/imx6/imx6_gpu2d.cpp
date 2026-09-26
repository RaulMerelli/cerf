#include "imx6_gpu.h"

namespace {
class Imx6Gpu2d final : public imx6_vivante::Imx6Gpu<0x00134000u, imx6_vivante::VivanteCore::Gc3202d, 10> {
public:
    using Imx6Gpu::Imx6Gpu;
};
}
REGISTER_SERVICE(Imx6Gpu2d);
