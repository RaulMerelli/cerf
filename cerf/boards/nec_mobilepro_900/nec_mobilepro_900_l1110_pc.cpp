#include "nec_mobilepro_900_l1110.h"

#include "../../core/cerf_emulator.h"

namespace {

/* PC Card socket NMC1110 register, PA 0x0A000000 (PXA255 socket 1). */
class NecMobilePro900L1110Pc : public NecMobilePro900L1110 {
public:
    using NecMobilePro900L1110::NecMobilePro900L1110;
    uint32_t MmioBase() const override { return 0x0A000000u; }
protected:
    int Socket() const override { return 1; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900L1110Pc);
