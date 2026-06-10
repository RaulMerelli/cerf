#pragma once

#include "cerf_virt_grad_descriptor.h"
#include "cerf_virt_blt_surface.h"
#include "../../core/service.h"

namespace CerfVirt {

/* Host GPE gradient fill: a linear colour ramp across the fill rect, faithful
   to the CE gradient renderer. Sibling of the blit / line operations. */
class CerfVirtGradientFiller : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfGradDescriptor& g);
};

}  /* namespace CerfVirt */
