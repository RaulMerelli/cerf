#pragma once

#include "../../core/service.h"

#include <cstdint>

/* CERF replaces the bootloader, so it seeds the persistent display-mode-select
   the bootloader writes into reserved LowMemory; the S1D13806 driver sizes its
   VRAM allocator from this field, and a zeroed value makes it too small for the
   screen surface, so the display never enables. Mode 2 = 640x240; the field
   offset differs per ROM generation, hence one impl per CE version. */
class NecMobilePro900BootloaderSeeder : public Service {
public:
    using Service::Service;
    void OnReady() override;

protected:
    /* PA of the LowMemory display-mode-select field for this ROM generation
       (LowMemory PA 0xA0004000 + the driver's struct offset). */
    virtual uint32_t DisplayModeSelectPa() const = 0;

    /* True iff this is the NEC MobilePro 900, guest additions are off, and the
       kernel subsystem major version matches this generation. */
    bool BoardMatchesKernelMajor(uint16_t major) const;

private:
    void Write();
};
