#include "nec_mobilepro_900_bootloader_seeder.h"

#include "../../core/cerf_emulator.h"

namespace {

/* HPC2000 = Windows CE 3.0. The S1D13806 driver reads the display-mode-select
   at LowMemory+0x1A858 (ddi.dll GPE sub_11B24DC). */
class NecMobilePro900Ce3BootloaderSeeder
    : public NecMobilePro900BootloaderSeeder {
public:
    using NecMobilePro900BootloaderSeeder::NecMobilePro900BootloaderSeeder;

    bool ShouldRegister() override { return BoardMatchesKernelMajor(3); }

protected:
    uint32_t DisplayModeSelectPa() const override { return 0xA001E858u; }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro900Ce3BootloaderSeeder,
                    NecMobilePro900BootloaderSeeder);
