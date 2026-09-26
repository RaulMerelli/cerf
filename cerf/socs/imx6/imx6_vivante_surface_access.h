#pragma once

#include <cstddef>
#include <cstdint>

#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

class VivanteSurfaceAccess : protected VivanteBlitOps {
protected:
    explicit VivanteSurfaceAccess(VivanteMem& mem) : mem_(mem) {}

    uint32_t RequireMemoryDeFormat(uint32_t fmt, const char* operation) const;
    static bool AddGpuOffset(uint32_t base, size_t offset, uint32_t& address);

    bool ReadPackedGpu(uint32_t base, size_t offset, uint32_t byte_count, uint32_t endian, uint32_t& packed,
                       MmuClient client = MmuClient::Texture) const;
    bool WritePackedGpu(uint32_t base, size_t offset, uint32_t byte_count, uint32_t endian, uint32_t packed,
                        MmuClient client = MmuClient::PixelEngine) const;
    bool ReadLayoutPackedGpu(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                             uint32_t byte_count, SurfaceLayout layout, uint32_t endian, uint32_t& packed,
                             bool supertiled_new = false, MmuClient client = MmuClient::Texture) const;
    bool WriteLayoutPackedGpu(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                              uint32_t byte_count, SurfaceLayout layout, uint32_t endian, uint32_t packed,
                              bool supertiled_new = false, MmuClient client = MmuClient::PixelEngine) const;
    bool ReadSurfacePackedGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                    uint32_t fmt, SurfaceLayout layout, uint32_t& packed, bool supertiled_new = false,
                                    uint32_t endian = 0u, MmuClient client = MmuClient::Texture) const;
    bool ReadSurfacePixelGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                   uint32_t fmt, uint32_t swizzle, SurfaceLayout layout, uint32_t& argb,
                                   bool supertiled_new = false, uint32_t endian = 0u,
                                   MmuClient client = MmuClient::Texture) const;
    bool WriteSurfacePixelGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                                    uint32_t fmt, uint32_t swizzle, SurfaceLayout layout, uint32_t argb,
                                    bool supertiled_new = false, uint32_t endian = 0u,
                                    MmuClient client = MmuClient::PixelEngine) const;
    bool ReadSurfacePackedGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, bool tiled,
                              bool supertiled, uint32_t& packed, bool supertiled_new = false, uint32_t endian = 0u,
                              MmuClient client = MmuClient::Texture) const;
    bool ReadSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, uint32_t swizzle,
                             bool tiled, bool supertiled, uint32_t& argb, bool supertiled_new = false,
                             uint32_t endian = 0u, MmuClient client = MmuClient::Texture) const;
    bool WriteSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt, uint32_t swizzle,
                              bool tiled, bool supertiled, uint32_t argb, bool supertiled_new = false,
                              uint32_t endian = 0u, MmuClient client = MmuClient::PixelEngine) const;
    bool ApplyPe10ClearGpu(uint32_t base, size_t offset, uint32_t pixel_bytes, uint32_t low, uint32_t high,
                           uint32_t byte_mask, uint32_t endian) const;
    bool ReadVrYuvPixelGpu(uint32_t y_address, uint32_t y_extra_address, uint32_t y_stride, uint32_t u_address,
                           uint32_t u_stride, uint32_t v_address, uint32_t v_stride, uint32_t x, uint32_t y,
                           uint32_t fmt, SurfaceLayout layout, uint32_t endian, bool uv_swizzle, bool bt709,
                           uint32_t& argb) const;

    VivanteMem& mem_;
};

}
