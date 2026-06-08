#pragma once

#include "../../core/service.h"

/* Board-side routing of the PD6710 -INTR (card-status-change) pin,
   one pulse per status-change event (BSP pcc_smdk2410.reg "CSCIrq",
   pdsocket.cpp InstallIsr). */
class Pd6710ManagementIrqLine : public Service {
public:
    using Service::Service;
    ~Pd6710ManagementIrqLine() override = default;

    virtual void Pulse() = 0;
};
