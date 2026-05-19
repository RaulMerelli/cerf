#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <mutex>

class Sa1110Intc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x90050000u; }
    uint32_t MmioSize() const override { return 0x00010000u; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* §9.2.1.1 source bit assignments — used by SoC peripheral
       impls to wire their interrupt source into the controller. */
    void AssertSource(uint32_t bit_index);
    void DeassertSource(uint32_t bit_index);

    /* Diagnostic snapshot — locks state_mtx_ so all four are
       consistent for a trace handler. */
    uint32_t GetIcpr() const;
    uint32_t GetIcmr() const;
    uint32_t GetIclr() const;
    uint32_t GetIcIp() const;

private:
    /* Drop and host-thread AssertSource racing JIT-thread WriteReg
       commits a stale jit.SetInterruptPending after the mask write
       already cleared the line — guest spins in OEMIH returning
       SYSINTR_NOP forever. */
    mutable std::mutex state_mtx_;

    uint32_t icpr_ = 0;
    uint32_t icmr_ = 0;
    uint32_t iclr_ = 0;
    uint32_t iccr_ = 0;

    uint32_t IcIpLocked() const { return icpr_ & icmr_ & ~iclr_; }
    uint32_t IcFpLocked() const { return icpr_ & icmr_ & iclr_; }

    void NotifyLocked();

    uint32_t ReadRegLocked(uint32_t off) const;
    void     WriteRegLocked(uint32_t off, uint32_t value);

    static bool IsKnown(uint32_t off) {
        return off == 0x00 || off == 0x04 || off == 0x08 ||
               off == 0x0C || off == 0x10 || off == 0x20;
    }
};
