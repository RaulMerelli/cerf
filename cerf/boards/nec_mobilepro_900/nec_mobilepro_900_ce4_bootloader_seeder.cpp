#include "nec_mobilepro_900_bootloader_seeder.h"

#include "../../core/cerf_emulator.h"

namespace {

/* Windows CE .NET 4.x. The S1D13806 driver and SABOOT read the
   display-mode-select at LowMemory+0x1A854 (SABOOT nk.exe sub_9006D3C4:
   2 -> 640x240, 1 -> 320x240, 0 -> 640x480). */
class NecMobilePro900Ce4BootloaderSeeder
    : public NecMobilePro900BootloaderSeeder {
public:
    using NecMobilePro900BootloaderSeeder::NecMobilePro900BootloaderSeeder;

    bool ShouldRegister() override { return BoardMatchesKernelMajor(4); }

protected:
    uint32_t DisplayModeSelectPa() const override { return 0xA001E854u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900Ce4BootloaderSeeder,
                    NecMobilePro900BootloaderSeeder);
