#pragma once

#include "../../core/service.h"

#include <cstdint>

class StateWriter;
class StateReader;

/* Codec on the PR31x00 SIB's subframe 0. SF0AUX carries REGADDR[3:0]<30:27>,
   WRITE<26> and REGDATA[15:0]; a read's reply is latched into SF0STAT. */
class Pr31x00SibCodec : public Service {
public:
    using Service::Service;

    virtual uint16_t ReadReg(uint8_t reg) = 0;
    virtual void     WriteReg(uint8_t reg, uint16_t value) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};
