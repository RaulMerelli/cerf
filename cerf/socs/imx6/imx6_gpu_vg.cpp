#include "imx6_gpu.h"

namespace {
class Imx6GpuVg final : public Imx6Gpu<0x02204000u, imx6_vivante::VivanteCore::Gc355Vg, 11> {
public:
    using Imx6Gpu::Imx6Gpu;

};
} // namespace
REGISTER_SERVICE(Imx6GpuVg);
