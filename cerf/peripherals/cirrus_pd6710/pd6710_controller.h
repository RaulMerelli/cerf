#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class Pd6710Card;
class Pd6710CardIrqLine;

class Pd6710Controller : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteByte (uint32_t addr, uint8_t  value) override;
    void     WriteHalf (uint32_t addr, uint16_t value) override;

    bool IsCardInserted() const;
    bool IsCardPowered () const;
    bool MapIoAddress  (uint32_t* io_offset) const;

    /* Returns the inserted card (nullptr if socket is empty). The
       window peripherals call ReadByte / ReadMemory* / WriteByte /
       WriteMemory* on the returned card directly. */
    Pd6710Card* Card() const;

    /* Card-side IRQ signaling. The card calls these from its own
       state lock; thread-safe — RaiseCardIrq itself takes the
       controller's state mutex only briefly to read the IRQ-enable
       mask, then drops it before driving the line. */
    void RaiseCardIrq();
    void ClearCardIrq();

private:
    uint8_t ReadIndexedData();
    void    WriteIndexedData(uint8_t value);

    mutable Pd6710Card*        card_     = nullptr;
    mutable Pd6710CardIrqLine* irq_line_ = nullptr;
    Pd6710Card*        ResolveCard()    const;
    Pd6710CardIrqLine* ResolveIrqLine() const;

    mutable std::mutex state_mutex_;

    /* PD6710 internal-register file. INDEX selects which register
       offset +0x01 reads / writes. */
    uint8_t index_ = 0u;

    uint8_t reg_chip_info_              = 0u;
    uint8_t reg_interface_status_       = 0u;
    uint8_t reg_power_control_          = 0u;
    uint8_t reg_card_status_change_     = 0u;
    uint8_t reg_status_change_int_cfg_  = 0u;
    uint8_t reg_window_enable_          = 0u;
    uint8_t reg_io_window_control_      = 0u;
    uint8_t reg_interrupt_and_gen_ctrl_ = 0u;

    /* I/O window mapping registers. Each window has START and END
       addresses as little-endian halfwords (Lo + Hi). Driver writes
       these before enabling the window via REG_WINDOW_ENABLE. */
    uint8_t reg_io_map0_start_lo_ = 0u;
    uint8_t reg_io_map0_start_hi_ = 0u;
    uint8_t reg_io_map0_end_lo_   = 0u;
    uint8_t reg_io_map0_end_hi_   = 0u;
    uint8_t reg_io_map1_start_lo_ = 0u;
    uint8_t reg_io_map1_start_hi_ = 0u;
    uint8_t reg_io_map1_end_lo_   = 0u;
    uint8_t reg_io_map1_end_hi_   = 0u;
};
