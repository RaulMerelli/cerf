#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class Omap3530Dss : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x48050000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* Called from HostWindow::TickAndPresent (UI thread, ~60 Hz)
       via the LcdScanTick concrete. Sets VSYNC in IRQSTATUS,
       clears GOLCD/GODIGITAL, recomputes INTC line. */
    void     AdvanceScanTick();

    bool     IsScanning();
    uint32_t GetFbPa();
    uint32_t GetGuestW();
    uint32_t GetGuestH();
    uint32_t GetGfxFormat();

private:
    static constexpr uint32_t kDispcBase  = 0x400u;
    static constexpr uint32_t kRfbiBase   = 0x800u;
    static constexpr uint32_t kVencBase   = 0xC00u;
    static constexpr uint32_t kBlockSize  = 0x400u;
    static constexpr uint32_t kBlockWords = kBlockSize / 4u;

    static constexpr uint32_t kDssRev       = 0x000u;
    static constexpr uint32_t kDssSysconfig = 0x010u;
    static constexpr uint32_t kDssSysstatus = 0x014u;
    static constexpr uint32_t kDssControl   = 0x040u;
    static constexpr uint32_t kDssSdiCtrl   = 0x044u;
    static constexpr uint32_t kDssPllCtrl   = 0x048u;
    static constexpr uint32_t kDssSdiStatus = 0x05Cu;

    static constexpr uint32_t kDispcRev        = 0x000u;
    static constexpr uint32_t kDispcSysconfig  = 0x010u;
    static constexpr uint32_t kDispcSysstatus  = 0x014u;
    static constexpr uint32_t kDispcIrqstatus  = 0x018u;
    static constexpr uint32_t kDispcIrqenable  = 0x01Cu;
    static constexpr uint32_t kDispcControl    = 0x040u;
    static constexpr uint32_t kDispcLineStatus = 0x05Cu;
    static constexpr uint32_t kDispcSizeLcd    = 0x07Cu;
    static constexpr uint32_t kDispcGfxBa0     = 0x080u;
    static constexpr uint32_t kDispcGfxAttribs = 0x0A0u;

    static constexpr uint32_t kSysconfigSoftReset = 1u << 1;
    static constexpr uint32_t kSysstatusResetDone = 1u << 0;

    static constexpr uint32_t kCtrlLcdEnable     = 1u << 0;
    static constexpr uint32_t kCtrlDigitalEnable = 1u << 1;
    static constexpr uint32_t kCtrlGoLcd         = 1u << 5;
    static constexpr uint32_t kCtrlGoDigital     = 1u << 6;

    static constexpr uint32_t kGfxAttrEnable = 1u << 0;

    static constexpr uint32_t kIrqFrameDone   = 1u << 0;
    static constexpr uint32_t kIrqVsync       = 1u << 1;
    static constexpr uint32_t kIrqEvsyncEven  = 1u << 2;
    static constexpr uint32_t kIrqEvsyncOdd   = 1u << 3;
    static constexpr uint32_t kIrqMask        = 0x1FFFFu;

    static constexpr int kIrqDss = 25;

    bool ShouldAssertIrqLocked() const;
    void RecomputeIrqLineLocked();
    void HandleDssSysconfigWriteLocked(uint32_t value);
    void HandleDispcSysconfigWriteLocked(uint32_t value);
    void HandleDispcIrqstatusWriteLocked(uint32_t value);
    void HandleDispcIrqenableWriteLocked(uint32_t value);
    void HandleDispcControlWrite(uint32_t old_value, uint32_t new_value);

    mutable std::mutex state_mutex_;

    uint32_t dss_top_[kBlockWords]{};
    uint32_t dispc_  [kBlockWords]{};
    uint32_t rfbi_   [kBlockWords]{};
    uint32_t venc_   [kBlockWords]{};

    bool irq_line_high_ = false;
};
