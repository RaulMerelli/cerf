#include "siemens_mp377_debug_leds.h"

#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../board_context.h"

#include <array>
#include <cstdint>

namespace {

class SiemensMp377DebugLedPeripheral : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint8_t ReadByte(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 debug LED byte read", addr, 0);
    }

    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 debug LED halfword read", addr, 0);
    }

    uint32_t ReadWord(uint32_t addr) override {
        HaltUnsupportedAccess("MP377 debug LED word read", addr, 0);
    }

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

/* WinCE OALLED/OEMWriteDebugLED target registers for indices 1..3.  The OAL
   writes these as halfwords only:

       index 1 -> PA 0xF2FFFFFC
       index 2 -> PA 0xF2FFFFFA
       index 3 -> PA 0xF2FFFFF6

   The sparse 8-byte aperture is deliberate: the unimplemented gap still halts,
   while the neighbouring EBUS1 guards remain non-overlapping. */
class SiemensMp377DebugLedProgressRegisters : public SiemensMp377DebugLedPeripheral {
public:
    using SiemensMp377DebugLedPeripheral::SiemensMp377DebugLedPeripheral;

    uint32_t MmioBase() const override { return siemens_mp377::kDebugLedProgressBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kDebugLedProgressEnd - MmioBase(); }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr) {
            case 0xF2FFFFFCu: values_[1] = value; return;
            case 0xF2FFFFFAu: values_[2] = value; return;
            case 0xF2FFFFF6u: values_[3] = value; return;
            default: break;
        }
        SiemensMp377DebugLedPeripheral::WriteHalf(addr, value);
    }

private:
    std::array<uint16_t, 4> values_{};
};

/* WinCE OALLED/OEMWriteDebugLED target register for index 0.  The P377 OAL
   periodically writes the timer value shifted by 10 to this halfword register
   during early kernel bring-up. */
class SiemensMp377DebugLedTickRegister : public SiemensMp377DebugLedPeripheral {
public:
    using SiemensMp377DebugLedPeripheral::SiemensMp377DebugLedPeripheral;

    uint32_t MmioBase() const override { return siemens_mp377::kDebugLedTickBase; }
    uint32_t MmioSize() const override { return siemens_mp377::kDebugLedTickEnd - MmioBase(); }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr == siemens_mp377::kDebugLedTickBase) {
            value_ = value;
            return;
        }
        SiemensMp377DebugLedPeripheral::WriteHalf(addr, value);
    }

private:
    uint16_t value_{};
};

} // namespace

REGISTER_SERVICE(SiemensMp377DebugLedProgressRegisters);
REGISTER_SERVICE(SiemensMp377DebugLedTickRegister);

