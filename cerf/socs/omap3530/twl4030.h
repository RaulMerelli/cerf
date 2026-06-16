#pragma once

#include "../i2c_slave.h"

#include <array>
#include <cstdint>
#include <mutex>

class Twl4030 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;

    bool MatchesAddress(uint8_t slave_addr) const override;

    void    TxnStart    (uint8_t slave_addr) override;
    void    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte (uint8_t slave_addr) override;

private:
    static int AddrIndex(uint8_t slave_addr);

    static constexpr int kAddrCount = 4;
    mutable std::mutex state_mutex_;
    std::array<std::array<uint8_t, 256>, kAddrCount> regs_{};
    std::array<uint8_t, kAddrCount>                  sub_addr_{};
    std::array<bool,    kAddrCount>                  sub_addr_pending_{};
};
