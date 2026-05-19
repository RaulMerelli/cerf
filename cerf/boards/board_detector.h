#pragma once

#include "../core/service.h"

#include <cstdint>
#include <string>
#include <vector>

enum class SocFamily {
    Unknown,
    S3C2410,
    SA1110,
    PXA27x,
    OMAP3530,
    Poseidon,
    iMX31,
    iMX32,
    TegraAPX,
};

enum class Board {
    Unknown,
    Smdk2410DevEmu,   /* Microsoft DeviceEmulator BSP on Samsung SMDK2410 */
    OdoArm720,        /* Microsoft Odo CE3 reference platform + Philips
                         Poseidon peripheral ASIC, ARM720T CPU socket.
                         BSP: references/WINCE300/PLATFORM/ODO with
                         _TGTCPUTYPE=THUMB, _TGTCPU=ARM720. */
    OmapEvm3530,      /* TI OMAP 3530 EVM (Cortex-A8 / CE7); EVM1 and EVM2 ship same BSP */
    Ipaq3650,         /* Compaq iPAQ H3000-platform Pocket PC 2000 (H3650 SKU), Intel SA-1110 */
};

class BoardDetector : public Service {
public:
    using Service::Service;
    void OnReady() override;

    virtual Board       GetBoard()    const = 0;
    virtual SocFamily   GetSoc()      const = 0;
    virtual const char* BoardName()   const = 0;

    static const char*  SocFamilyName(SocFamily f);

protected:
    /* NameContains is ASCII-only — UTF-16 needles need ContainsString.
       ModuleNames omits IMGFS-table filenames (WM6+ NB0) — those need
       RomContainsString. */
    std::string          ModuleNames   () const;
    std::vector<uint8_t> ReadKernelBlob() const;
    bool                 RomContainsString(const char* needle) const;
    static bool          ContainsString(const std::vector<uint8_t>& bytes,
                                        const char* needle);
    static bool          NameContains  (const std::string& names,
                                        const char* needle);
};
