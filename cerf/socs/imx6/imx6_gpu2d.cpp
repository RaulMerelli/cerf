#include "imx6_gpu.h"

namespace {
class Imx6Gpu2d final : public Imx6Gpu<0x00134000u, imx6_vivante::VivanteCore::Gc3202d, 10> {
public:
    using Imx6Gpu::Imx6Gpu;

};
} // namespace
REGISTER_SERVICE(Imx6Gpu2d);
