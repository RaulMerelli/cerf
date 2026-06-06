#pragma once

#include "../../core/service.h"

#include <cstdint>

/* Slave on the SA-1110 SSP (Motorola SPI, full duplex): every TX frame
   clocks one RX frame back. The board's concrete (e.g. the Jornada 720
   MCU) registers via REGISTER_SERVICE_AS; boards without an SSP slave
   have no winner and the SSP RX FIFO simply stays empty. */
class Sa1110SspDevice : public Service {
public:
    using Service::Service;

    virtual uint16_t Exchange(uint16_t tx_frame) = 0;
};
