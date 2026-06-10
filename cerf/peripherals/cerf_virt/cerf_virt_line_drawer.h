#pragma once

#include "cerf_virt_line_descriptor.h"
#include "cerf_virt_blt_surface.h"
#include "../../core/service.h"

namespace CerfVirt {

/* Host GPE line draw: styled Bresenham segment, faithful replay of
   GPE::EmulatedLine. Sibling of the blit / gradient operations. */
class CerfVirtLineDrawer : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfLineDescriptor& l);
};

}  /* namespace CerfVirt */
