#include "imx6_gpu.h"

namespace {
class Imx6Gpu3d final : public Imx6Gpu<0x00130000u, imx6_vivante::VivanteCore::Gc8803d, 9> {
public:
    using Imx6Gpu::Imx6Gpu;

};
} // namespace
REGISTER_SERVICE(Imx6Gpu3d);
