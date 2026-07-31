#include "sa11xx_processor_config_base.h"

#include <intrin.h>

#include "../../jit/arm/decoded_insn.h"
#include "../../jit/arm/place_fns.h"

uint16_t Sa11xxProcessorConfigBase::CycleCostFor(const DecodedInsn& d) const {
    if (d.place_fn == &PlaceBlockDataTransfer) {
        /* LDM/STM = MAX(2, registers). */
        const unsigned n = __popcnt16(d.register_list);
        return static_cast<uint16_t>(n < 2 ? 2 : n);
    }
    if (d.place_fn == &PlaceMRSorMSR) {
        /* d.s == 1 marks MSR (control write); MRS read = 1. */
        return d.s ? 3 : 1;
    }
    if (d.place_fn == &PlaceMSRImmediate) return 3;
    return 1;
}
