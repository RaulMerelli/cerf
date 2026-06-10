#include "intel_28f128j3.h"

namespace {

/* nCS1 data/FFS bank (PA 0x08000000, uncached alias 0xA2000000). Two x16 chips
   in 2-way interleave -> 32-bit bus: OAL detect sub_800B442C (nk.exe) reads mfr
   0x00890089 / device 0x00180018 (width=4/parallel=2). */
class Intel28F128J3Cs1 : public Intel28F128J3 {
public:
    using Intel28F128J3::Intel28F128J3;

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

protected:
    uint32_t Parallel()    const override { return 2u; }
    uint32_t DeviceWidth() const override { return 2u; }
};

}  /* namespace */

REGISTER_SERVICE(Intel28F128J3Cs1);
