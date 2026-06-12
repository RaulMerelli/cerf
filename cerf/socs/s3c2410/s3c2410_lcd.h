#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>

/* IsEnabled() halts on ENVID=1 with PNRMODE/BPPMODE/FRM565 not
   matching the modeled 16bpp 5:6:5 TFT path — silently accepting
   would render wrong colors. Register layout mirrored from
   references/WINCE600/PLATFORM/DEVICEEMULATOR/SRC/INC/s3c2410x_lcd.h. */

class S3C2410Lcd : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x4D000000u; }
    uint32_t MmioSize() const override { return 0x00100000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    bool     IsEnabled();
    uint32_t GetFbPa();
    uint32_t GetGuestW();
    uint32_t GetGuestH();

private:
    static constexpr uint32_t kCtrlOff   = 0x000u;
    static constexpr uint32_t kCtrlEnd   = 0x064u;        /* exclusive */
    static constexpr uint32_t kPalOff    = 0x400u;
    static constexpr uint32_t kPalEnd    = 0x800u;        /* exclusive */
    static constexpr uint32_t kCtrlCount = (kCtrlEnd - kCtrlOff) / 4u;
    static constexpr uint32_t kPalCount  = (kPalEnd  - kPalOff)  / 4u;

    enum CtrlIndex : uint32_t {
        kIdxLCDCON1   =  0,
        kIdxLCDCON2   =  1,
        kIdxLCDCON3   =  2,
        kIdxLCDCON4   =  3,
        kIdxLCDCON5   =  4,
        kIdxLCDSADDR1 =  5,
        kIdxLCDSADDR2 =  6,
        kIdxLCDSADDR3 =  7,
    };

    static constexpr uint32_t kPnrmodeTft       = 3u;
    static constexpr uint32_t kBppmode16bppTft  = 12u;

    uint32_t* DecodeSlot(uint32_t addr);

    uint32_t ctrl_[kCtrlCount] = {};
    uint32_t pal_ [kPalCount]  = {};
};
