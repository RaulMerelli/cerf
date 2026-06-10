#include "intel_28f128j3.h"

namespace {

/* nCS0 boot flash (PA 0, uncached 0xA4000000): one x16 28F128J3, 16 MB (Linux
   simpad mach CS0 = SZ_16M); 16-bit bus -> CFI word 0x10 at byte 0x20. The CFI
   consumer sizes the bank as per-chip 16 MB * Parallel(), so parallel=1 keeps
   its reads inside the 16 MB EmulatedMemory backing. */
class Intel28F128J3Cs0 : public Intel28F128J3 {
public:
    using Intel28F128J3::Intel28F128J3;

    uint32_t MmioBase() const override { return 0x00000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

protected:
    uint32_t Parallel()    const override { return 1u; }
    uint32_t DeviceWidth() const override { return 2u; }
};

}  /* namespace */

REGISTER_SERVICE(Intel28F128J3Cs0);
