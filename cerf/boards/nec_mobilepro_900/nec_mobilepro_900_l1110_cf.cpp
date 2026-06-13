#include "nec_mobilepro_900_l1110.h"

#include "../../core/cerf_emulator.h"

namespace {

/* CF socket NMC1110 register, PA 0x09000000 (PXA255 socket 0). */
class NecMobilePro900L1110Cf : public NecMobilePro900L1110 {
public:
    using NecMobilePro900L1110::NecMobilePro900L1110;
    uint32_t MmioBase() const override { return 0x09000000u; }
protected:
    int Socket() const override { return 0; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilePro900L1110Cf);
