#pragma once

#include "../../core/service.h"
#include "../../socs/pr31x00/pr31x00_card_space.h"

#include <cstdint>

class PcmciaSlot;
class StateReader;
class StateWriter;

/* The chip drives one INT pin, shared by its card-status and card-interrupt
   sources (NetBSD `it8368_intr` demultiplexes them from GPIONEGINTSTAT). The
   board owns where that pin lands. */
class IteIt8368IntSink {
public:
    virtual ~IteIt8368IntSink() = default;
    virtual void OnIt8368IntLevel(bool asserted) = 0;
};

/* ITE IT8368E PCMCIA/GPIO buffer chip. 16-bit registers at even offsets
   $00-$20; ReadReg/WriteReg take the chip's own bit order, so a board whose
   bus swaps the halves converts before calling. */
class IteIt8368 : public Service, public Pr31x00CardBuffer {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetSlot(PcmciaSlot* slot) { slot_ = slot; }
    void SetIntSink(IteIt8368IntSink* sink) { int_sink_ = sink; }

    uint16_t ReadReg(uint32_t off);
    void     WriteReg(uint32_t off, uint16_t value);

    /* Pr31x00CardBuffer. */
    bool CardInterfaceEnabled() const override;
    bool FixedAttributeIo() const override;

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    uint16_t GpioDataIn() const;
    bool     IntLevel() const;
    void     LatchInputEdges();
    void     ApplyOutputs();
    void     UpdateInt();

    PcmciaSlot*       slot_     = nullptr;
    IteIt8368IntSink* int_sink_ = nullptr;

    uint16_t gpio_dataout_    = 0;
    uint16_t gpio_dir_        = 0;
    uint16_t gpio_posinten_   = 0;
    uint16_t gpio_neginten_   = 0;
    uint16_t gpio_posintstat_ = 0;
    uint16_t gpio_negintstat_ = 0;
    uint16_t mfio_posintstat_ = 0;
    uint16_t mfio_negintstat_ = 0;
    uint16_t mfio_dataout_    = 0;
    uint16_t mfio_dir_        = 0;
    uint16_t mfio_sel_        = 0;
    uint16_t ctrl_            = 0;
    bool     int_asserted_    = false;

    /* Every GPIO starts undriven, and an undriven card pin floats high. */
    uint16_t prev_datain_  = 0x1FFF;
    bool     rst_asserted_ = false;
};
