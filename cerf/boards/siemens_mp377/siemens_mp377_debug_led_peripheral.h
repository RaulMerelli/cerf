#pragma once

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_context.h"

class SiemensMp377DebugLedPeripheral : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("MP377 debug LED byte read", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("MP377 debug LED halfword read", addr, 0); }
    uint32_t ReadWord(uint32_t addr) override { HaltUnsupportedAccess("MP377 debug LED word read", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("MP377 debug LED byte write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("MP377 debug LED halfword write", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("MP377 debug LED word write", addr, value);
    }

};
