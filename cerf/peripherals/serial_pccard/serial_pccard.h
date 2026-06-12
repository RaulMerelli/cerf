#pragma once

#include "../pcmcia/pcmcia_card.h"
#include "serial_16550.h"

#include <cstdint>
#include <memory>
#include <vector>

class SerialEndpoint;

/* Generic 16-bit serial / modem PC Card: a PC16550D (Serial16550) behind a
   SerialEndpoint personality, presented so CE's in-box serial.dll binds it as
   COMx: with no device-specific driver. CIS + attribute config registers (COR/
   FCSR) + I/O map are verified against the WinCE6 DDK SERIAL/PCCARD source. */
class SerialPcCard : public PcmciaCard {
public:
    static constexpr const wchar_t* kDisplayName        = L"Serial Modem PC Card";
    static constexpr const wchar_t* kForwardDisplayName = L"Serial Port Forwarder";

    explicit SerialPcCard(CerfEmulator& emu);                    /* modem personality */
    SerialPcCard(CerfEmulator& emu, std::wstring host_com_port); /* host-port forward */
    ~SerialPcCard() override;

    std::wstring DisplayName() const override {
        return mode_ == Mode::HostForward ? kForwardDisplayName : kDisplayName;
    }
    std::wstring TooltipDetail() const override;

    void OnInserted() override;

    const char* SaveId() const override {
        return mode_ == Mode::HostForward ? "serial_fwd" : "serial";
    }
    std::wstring SaveBinding() const override {
        return mode_ == Mode::HostForward ? host_port_ : std::wstring();
    }
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void OnShutdown() override;

    void PowerOn () override;
    void PowerOff() override;

    uint8_t ReadAttribute8 (uint32_t offset)                override;
    void    WriteAttribute8(uint32_t offset, uint8_t value) override;

    uint8_t  ReadCommon8  (uint32_t offset)                 override;
    uint16_t ReadCommon16 (uint32_t offset)                 override;
    void     WriteCommon8 (uint32_t offset, uint8_t  value) override;
    void     WriteCommon16(uint32_t offset, uint16_t value) override;

    uint8_t  ReadIo8  (uint32_t offset)                     override;
    uint16_t ReadIo16 (uint32_t offset)                     override;
    void     WriteIo8 (uint32_t offset, uint8_t  value)     override;
    void     WriteIo16(uint32_t offset, uint16_t value)     override;

private:
    void OnUartIrq(bool asserted);
    void BuildCis();   /* mode-dependent CIS (FUNCE present only for the modem) */

    /* Attribute config registers: ConfigBase from CISTPL_CONFIG, each register
       offset*2 apart (pcmcia.cpp config-register addressing; cardserv.h). */
    static constexpr uint32_t kConfigBase = 0x200u;
    static constexpr uint32_t kCorOffset  = kConfigBase + 0u;   /* FCR_OFFSET_COR  */
    static constexpr uint32_t kFcsrOffset = kConfigBase + 2u;   /* FCR_OFFSET_FCSR */
    /* I/O window: the 8 16550 registers at the CISTPL_CFTABLE_ENTRY base. */
    static constexpr uint32_t kIoBase   = 0x3F8u;
    static constexpr uint32_t kRegCount = 8u;

    uint8_t cor_      = 0;
    uint8_t fcsr_     = 0;
    bool    uart_irq_ = false;

    enum class Mode { Modem, HostForward };
    Mode         mode_ = Mode::Modem;
    std::wstring host_port_;     /* host COM name when mode_ == HostForward */
    std::vector<uint8_t> cis_;   /* attribute-memory CIS, built per mode */

    std::unique_ptr<SerialEndpoint> endpoint_;
    std::unique_ptr<Serial16550>    uart_;
};
