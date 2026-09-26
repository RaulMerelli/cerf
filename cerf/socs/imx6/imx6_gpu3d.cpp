#include "imx6_gpu.h"

namespace {
class Imx6Gpu3d final : public imx6_vivante::Imx6Gpu<0x00130000u, imx6_vivante::VivanteCore::Gc20003d, 9> {
public:
    using Imx6Gpu::Imx6Gpu;
};
}
REGISTER_SERVICE(Imx6Gpu3d);
