#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <string>

/* Philips PR31x00 UART, TMPR3911/3912 ch.16. The two ports share one register
   block: UART A at $0B0 and UART B at $0C8. */
class Pr31x00Uart : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioSize() const override { return 0x18u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    /* Names this port in the guest debug log. */
    virtual const char* TxSource() const = 0;

private:
    void WriteCtl1(uint32_t addr, uint32_t value);
    void WriteTxHold(uint32_t addr, uint32_t value);

    uint32_t    ctl1_ = 0;
    std::string tx_line_;
};
