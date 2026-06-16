#pragma once

#include "../../socs/i2c_slave.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"

#include <array>
#include <cstdint>

/* MC13892 (Atlas) PMIC — the SYNC2 board's power IC on i.MX51 I2C2, 7-bit slave
   address 0x08 (pmicPdk_mc13892.dll GetRegister/SetRegister). 64 24-bit
   registers accessed MSB-first: a register access is a 1-byte reg-index write
   followed by a 3-byte data write or read (I2CWrite/ReadThreeBytes). */
class Mc13892Pmic : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::FordSyncGen2;
    }

    bool MatchesAddress(uint8_t slave_addr) const override {
        return slave_addr == kAddr;
    }
    void    TxnStart    (uint8_t slave_addr) override;
    void    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte (uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override {
        for (uint32_t v : regs_) w.Write(v);
        w.Write(sub_addr_);
        w.Write<uint8_t>(sub_addr_pending_ ? 1 : 0);
        w.Write<uint8_t>(wr_idx_);
        w.Write<uint8_t>(rd_idx_);
    }
    void RestoreState(StateReader& r) override {
        for (uint32_t& v : regs_) r.Read(v);
        r.Read(sub_addr_);
        uint8_t b = 0;
        r.Read(b); sub_addr_pending_ = b != 0;
        r.Read(b); wr_idx_ = b;
        r.Read(b); rd_idx_ = b;
    }

private:
    static constexpr uint8_t kAddr     = 0x08u;
    static constexpr uint8_t kRegCount = 64u;

    std::array<uint32_t, kRegCount> regs_{};
    uint8_t sub_addr_         = 0;
    bool    sub_addr_pending_ = true;
    uint8_t wr_idx_           = 0;
    uint8_t rd_idx_           = 0;
};
