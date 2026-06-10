#pragma once

#include "cerf_virt_blt_descriptor.h"
#include "cerf_virt_blt_surface.h"
#include "../../core/service.h"

#include <cstdint>
#include <vector>

namespace CerfVirt {

/* Host GPE blit: composites a source / coverage into a destination per pixel
   (generic ROP3/alpha/stretch/mask/brush, plus grayscale AA text). Gradient
   fill and line draw are sibling operations in their own services. */
class CerfVirtBlitter : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfBltDescriptor& d);

private:
    bool BlendAAText(const CerfBltDescriptor& d, Surface& dst,
                     const uint32_t d_masks[3], uint32_t d_bpp);

    std::vector<int32_t> sx_lut_;   /* per-dst-col source offset (BLT_STRETCH) */
    std::vector<int32_t> sy_lut_;   /* per-dst-row source offset (BLT_STRETCH) */
};

}  /* namespace CerfVirt */
