#pragma once

#include "siemens_mp377_sm501.h"
#include "siemens_mp377_sm501_fb.h"
#include "siemens_mp377_sm501_dma.h"
#include "siemens_mp377_sm501_ac97.h"
#include "siemens_mp377_sm501_audio_output.h"
#include "siemens_mp377_sm501_audio_mcu.h"
#include "siemens_mp377_sm501_power_gpio.h"
#include "siemens_mp377_touch_panel.h"

#include "../../cpu/mcs51/mcs51_core.h"
#include "../../peripherals/peripheral_base.h"
#include "../../state/emulation_freeze.h"
#include "../../state/state_stream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace siemens_mp377 {

class SiemensMp377Sm501Blitter;

// Source trail for these SM501 compatibility registers:
// - Linux include/linux/sm501-regs.h names COMMAND_LIST_STATUS 0x24,
//   CURRENT_GATE 0x38, CURRENT_CLOCK 0x3c, DEVICEID 0x60, and documents
//   DEVICEID as 0x050100A0.
// - QEMU sm501 "Add some more unimplemented registers" returns
//   COMMAND_LIST_STATUS=0x00180002 as "FIFOs are empty, everything idle";
//   older QEMU SM501 reads return CURRENT_GATE=0x00021807 and
//   CURRENT_CLOCK=0x2A1A0A09.
inline constexpr uint32_t kSm501Gpio31_0ControlReg   = 0x000008u;
inline constexpr uint32_t kSm501CommandListStatusReg = 0x000024u;
inline constexpr uint32_t kSm501RawIrqStatusReg      = 0x000028u;
inline constexpr uint32_t kSm501IrqStatusReg         = 0x00002Cu;
inline constexpr uint32_t kSm501IrqMaskReg           = 0x000030u;
inline constexpr uint32_t kSm501CurrentGateReg       = 0x000038u;
inline constexpr uint32_t kSm501CurrentClockReg      = 0x00003Cu;
inline constexpr uint32_t kSm501PowerMode0GateReg    = 0x000040u;
inline constexpr uint32_t kSm501PowerMode0ClockReg   = 0x000044u;
inline constexpr uint32_t kSm501PowerMode1GateReg    = 0x000048u;
inline constexpr uint32_t kSm501PowerMode1ClockReg   = 0x00004Cu;
inline constexpr uint32_t kSm501SleepModeGateReg     = 0x000050u;
inline constexpr uint32_t kSm501PowerModeControlReg  = 0x000054u;
inline constexpr uint32_t kSm501DeviceIdReg          = 0x000060u;
inline constexpr uint32_t kSm501GpioDataLowReg       = 0x010000u;
inline constexpr uint32_t kSm501GpioDirectionLowReg  = 0x010008u;

inline constexpr uint32_t kSm501CommandListIdle      = 0x00180002u;
inline constexpr uint32_t kSm501PowerModeGateDefault = 0x00021807u;
inline constexpr uint32_t kSm501PowerModeClockDefault= 0x2A1A0A09u;
inline constexpr uint32_t kSm501SleepModeGateDefault = 0x00018000u;
inline constexpr uint32_t kSm501DeviceId             = 0x050100A0u;

inline constexpr uint32_t kSm501Gate8051SramBit      = 1u << 17;
inline constexpr uint32_t kSm501GateAc97I2sBit       = 1u << 18;
inline constexpr uint32_t kSm501Gpio24Ac97RstBit     = 1u << 24;
inline constexpr uint32_t kSm501Gpio25Ac97SyncBit    = 1u << 25;
inline constexpr uint32_t kSm501Gpio26Ac97BitclkBit  = 1u << 26;
inline constexpr uint32_t kSm501Gpio27Ac97SdoutBit   = 1u << 27;
inline constexpr uint32_t kSm501Gpio28Ac97SdinBit    = 1u << 28;
inline constexpr uint32_t kSm501GpioAc97Mask         =
    kSm501Gpio24Ac97RstBit | kSm501Gpio25Ac97SyncBit |
    kSm501Gpio26Ac97BitclkBit | kSm501Gpio27Ac97SdoutBit |
    kSm501Gpio28Ac97SdinBit;
inline constexpr uint32_t kSm501GpioAc97OutputMask   =
    kSm501Gpio24Ac97RstBit | kSm501Gpio25Ac97SyncBit |
    kSm501Gpio27Ac97SdoutBit;

inline constexpr uint32_t kMp377CodecResetGpioBit    = 1u << 9;
inline constexpr uint32_t kMp377CodecPowerGpioBit    = 1u << 11;
inline constexpr uint32_t kMp377CodecBoardGpioMask    =
    kMp377CodecResetGpioBit | kMp377CodecPowerGpioBit;

class SiemensMp377Sm501Regs : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t PanelFbOffset() const;
    uint32_t PanelPitchBytes() const;
    uint32_t ReadSm501Register(uint32_t offset) const;

    uint8_t ReadByte(uint32_t a) override;
    uint16_t ReadHalf(uint32_t a) override;
    uint32_t ReadWord(uint32_t a) override;
    void WriteByte(uint32_t a, uint8_t v) override;
    void WriteHalf(uint32_t a, uint16_t v) override;
    void WriteWord(uint32_t a, uint32_t v) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    friend class SiemensMp377Sm501Dma;
    friend class SiemensMp377Sm501Ac97;
    friend class SiemensMp377Sm501AudioOutput;
    friend class SiemensMp377Sm501AudioMcu;
    friend class SiemensMp377Sm501PowerGpio;
    template <typename T>
    void WriteVectorState(StateWriter& w, const std::vector<T>& v) const {
        const uint64_t n = static_cast<uint64_t>(v.size());
        w.Write(n);
        if (n) w.WriteBytes(v.data(), static_cast<size_t>(n * sizeof(T)));
    }

    template <typename T>
    void ReadVectorState(StateReader& r, std::vector<T>& v, size_t max_expected, const char* what) {
        uint64_t n = 0;
        r.Read(n);
        if (n > static_cast<uint64_t>(max_expected)) {
            HaltUnsupportedAccess(what, kSm501RegsBarPa, n);
        }
        v.resize(static_cast<size_t>(n));
        if (n) r.ReadBytes(v.data(), static_cast<size_t>(n * sizeof(T)));
    }

    static uint32_t NormalizePanelFbOffset(uint32_t v);
    static uint32_t DecodePanelPitchBytes(uint32_t v);
    static uint32_t Lo16(uint32_t v);
    static uint32_t Hi16(uint32_t v);

    static constexpr uint32_t kSm501SupportedIrqBits  = 0xFFFFFFFFu;
    uint32_t Sm501LatchedInterruptStatus() const;
    void RefreshSm501InterruptLine();
    void RaiseSm501InterruptBits(uint32_t bits);
    void ClearSm501InterruptBits(uint32_t bits);

    std::vector<uint32_t> regs_;
    uint32_t panel_fb_raw_ = 0u;
    uint32_t panel_pitch_bytes_ = 0u;

    bool sm501_irq_line_asserted_ = false;
};


class SiemensMp377SmiBridgeWindow : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint8_t ReadByte(uint32_t a) override;
    uint16_t ReadHalf(uint32_t a) override;
    uint32_t ReadWord(uint32_t a) override;
    void WriteByte(uint32_t a, uint8_t v) override;
    void WriteHalf(uint32_t a, uint16_t v) override;
    void WriteWord(uint32_t a, uint32_t v) override;

protected:
    virtual bool IsC410ConsoleAlias() const;

private:
    uint32_t RelativeOffset(uint32_t a) const;
    static bool IsSupportedOffset(uint32_t rel);
};

class SiemensMp377SmiBridgeC410 : public SiemensMp377SmiBridgeWindow {
public:
    using SiemensMp377SmiBridgeWindow::SiemensMp377SmiBridgeWindow;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

protected:
    bool IsC410ConsoleAlias() const override;

public:
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;
};

class SiemensMp377SmiBridgeC480 : public SiemensMp377SmiBridgeWindow {
public:
    using SiemensMp377SmiBridgeWindow::SiemensMp377SmiBridgeWindow;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
};

} // namespace siemens_mp377

