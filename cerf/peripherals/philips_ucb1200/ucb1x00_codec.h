#pragma once

#include "../../core/service.h"

#include <array>
#include <cstdint>

class StateWriter;
class StateReader;

/* Philips UCB1x00 modem/audio analog front-end. The SoC's serial port decodes a
   register command and calls ReadReg/WriteReg; the board's analog and I/O pins
   arrive through Ucb1x00Board. Register map and bit fields: Linux ucb1x00.h,
   cross-checked against NetBSD hpcmips ucb1200reg.h. */
class Ucb1x00Codec : public Service {
public:
    using Service::Service;

    uint16_t ReadReg(uint8_t reg);
    void     WriteReg(uint8_t reg, uint16_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

protected:
    /* ID_REG. UCB1200 reads 0x1004; the Nino's sib.dll compares its low 6 bits
       against 4 to decide the part is present. */
    virtual uint16_t DeviceId() const = 0;

private:
    uint16_t PenDetectTsCr() const;
    void     Convert(uint16_t adc_cr);

    std::array<uint16_t, 16> regs_{};
    uint16_t adc_data_ = 0;
};
