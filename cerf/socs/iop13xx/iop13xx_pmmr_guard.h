#pragma once

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"

class Iop13xxPmmrGuard : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::IOP13xx;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }
    uint32_t ReadWord(uint32_t addr) override { HaltUnsupportedAccess("IOP13xx unsupported PMMR word read", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR halfword read", addr, 0);
    }
    uint8_t ReadByte(uint32_t addr) override { HaltUnsupportedAccess("IOP13xx unsupported PMMR byte read", addr, 0); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR word write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR halfword write", addr, value);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("IOP13xx unsupported PMMR byte write", addr, value);
    }

};

template <uint32_t kBase, uint32_t kSize> class Iop13xxPmmrRange : public Iop13xxPmmrGuard {
public:
    using Iop13xxPmmrGuard::Iop13xxPmmrGuard;
    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }
};
