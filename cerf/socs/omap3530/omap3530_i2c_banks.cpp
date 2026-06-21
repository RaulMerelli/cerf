#include "omap3530_i2c.h"

#include "../../core/cerf_emulator.h"
#include "omap3530_sdma.h"
#include "twl4030.h"

namespace {

/* I2C1 - talks to the TWL4030 PMIC. Dispatch hooks routed to the
   TWL4030 slave service. */
class Omap3530I2c1 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48070000u; }

protected:
    int TxSyncSource() const override { return Omap3530Sdma::kSyncI2c1Tx; }
    int RxSyncSource() const override { return Omap3530Sdma::kSyncI2c1Rx; }
    void DispatchWriteLocked(uint32_t guest_addr_for_diag,
                             uint8_t  slave_addr) override;
    void DispatchReadLocked (uint32_t guest_addr_for_diag,
                             uint8_t  slave_addr,
                             uint16_t count) override;
};

void Omap3530I2c1::DispatchWriteLocked(uint32_t guest_addr_for_diag,
                                       uint8_t  slave_addr) {
    auto& twl = emu_.Get<Twl4030>();
    if (!twl.MatchesAddress(slave_addr)) {
        Omap3530I2cBank::DispatchWriteLocked(guest_addr_for_diag,
                                             slave_addr);
    }
    twl.TxnStart(slave_addr);
    while (!tx_fifo_.empty()) {
        twl.TxnWriteByte(slave_addr, tx_fifo_.front());
        tx_fifo_.pop_front();
    }
}

void Omap3530I2c1::DispatchReadLocked(uint32_t guest_addr_for_diag,
                                      uint8_t  slave_addr,
                                      uint16_t count) {
    auto& twl = emu_.Get<Twl4030>();
    if (!twl.MatchesAddress(slave_addr)) {
        Omap3530I2cBank::DispatchReadLocked(guest_addr_for_diag,
                                            slave_addr, count);
    }
    twl.TxnStart(slave_addr);
    for (uint16_t i = 0; i < count; ++i) {
        rx_fifo_.push_back(twl.TxnReadByte(slave_addr));
    }
}

class Omap3530I2c2 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48072000u; }
protected:
    int TxSyncSource() const override { return Omap3530Sdma::kSyncI2c2Tx; }
    int RxSyncSource() const override { return Omap3530Sdma::kSyncI2c2Rx; }
};
class Omap3530I2c3 : public Omap3530I2cBank {
public:
    using Omap3530I2cBank::Omap3530I2cBank;
    uint32_t MmioBase() const override { return 0x48060000u; }
    /* I2C3 is high-speed I2C with no SDMA wiring on OMAP3530 - uses
       its own internal FIFO and CPU-driven transfers. */
};

}  /* namespace */

REGISTER_SERVICE(Omap3530I2c1);
REGISTER_SERVICE(Omap3530I2c2);
REGISTER_SERVICE(Omap3530I2c3);
