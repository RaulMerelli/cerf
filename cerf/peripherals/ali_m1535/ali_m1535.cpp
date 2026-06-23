#include "../peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_detector.h"
#include "../peripheral_dispatcher.h"
#include "../intel_i8042/i8042_controller.h"
#include "../../state/state_stream.h"

#include <cstdint>


namespace {

constexpr uint32_t kBase = 0x14000000u;   /* BSP_REG_PA_PCI_IO */
constexpr uint32_t kSize = 0x00001000u;   /* legacy ISA I/O page */

enum : uint32_t {
    kKbcData = 0x060, kKbcStatus = 0x064,
    kPic1Cmd = 0x020, kPic1Data = 0x021,   /* BSP_REG_PA_M1535_PIC1 = PCI_IO + 0x20 */
    kPic2Cmd = 0x0A0, kPic2Data = 0x0A1,   /* BSP_REG_PA_M1535_PIC2 = PCI_IO + 0xA0 */
    kElcr1   = 0x4D0, kElcr2    = 0x4D1,    /* BSP_REG_PA_M1535_EDGE1/2 */
};

/* One Intel 8259A. The init sequence is ICW1 (cmd, bit4 set) then ICW2/ICW3/ICW4
   on the data port; afterward the data port is OCW1 (IMR), and the cmd port is
   OCW2 (EOI) or OCW3 (read-register select). Source: 8259A datasheet register
   model, exercised exactly as BSPIntrInit programs it. */
struct Pic8259 {
    uint8_t imr = 0;          /* OCW1 interrupt mask */
    uint8_t irr = 0;          /* interrupt request (set by sources - none during boot) */
    uint8_t isr = 0;          /* in-service */
    uint8_t icw1 = 0, icw2 = 0, icw3 = 0, icw4 = 0;
    uint8_t init_step = 0;    /* 0 = operational, 1=expect ICW2, 2=ICW3, 3=ICW4 */
    bool    icw4_needed = false;
    bool    single = false;
    bool    read_isr = false; /* OCW3 RIS: cmd-port read returns ISR vs IRR */

    void WriteCmd(uint8_t v) {
        if (v & 0x10) {                       /* ICW1 */
            icw1 = v; icw4_needed = v & 0x01; single = v & 0x02;
            imr = 0; isr = 0; read_isr = false; init_step = 1;
        } else if (v & 0x08) {                /* OCW3 */
            if (v & 0x02) read_isr = (v & 0x01);   /* RR set: RIS selects IRR/ISR */
        } else {                              /* OCW2 */
            if (v & 0x20) {                   /* EOI */
                if (v & 0x40) {               /* specific EOI: clear named level */
                    isr &= ~(1u << (v & 0x07));
                } else if (isr) {             /* non-specific EOI: clear highest priority */
                    for (uint8_t b = 0; b < 8; ++b) {
                        if (isr & (1u << b)) { isr &= ~(1u << b); break; }
                    }
                }
            }
        }
    }

    void WriteData(uint8_t v) {
        switch (init_step) {
            case 1: icw2 = v; init_step = single ? (icw4_needed ? 3 : 0) : 2; break;
            case 2: icw3 = v; init_step = icw4_needed ? 3 : 0; break;
            case 3: icw4 = v; init_step = 0; break;
            default: imr = v; break;          /* OCW1 */
        }
    }

    uint8_t ReadCmd()  const { return read_isr ? isr : irr; }
    uint8_t ReadData() const { return imr; }
};

class AliM1535 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        return bd && bd->GetBoard() == Board::NecRockhopper;
    }
    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte (uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    Pic8259 pic_[2];          /* [0] = master (0x20), [1] = slave (0xA0) */
    uint8_t elcr_[2] = {0, 0};
};

uint8_t AliM1535::ReadByte(uint32_t addr) {
    switch (addr - kBase) {
        case kKbcData:   return emu_.Get<I8042Controller>().ReadData();
        case kKbcStatus: return emu_.Get<I8042Controller>().ReadStatus();
        case kPic1Cmd:  return pic_[0].ReadCmd();
        case kPic1Data: return pic_[0].ReadData();
        case kPic2Cmd:  return pic_[1].ReadCmd();
        case kPic2Data: return pic_[1].ReadData();
        case kElcr1:    return elcr_[0];
        case kElcr2:    return elcr_[1];
        default:        HaltUnsupportedAccess("ReadByte", addr, 0);
    }
}

void AliM1535::WriteByte(uint32_t addr, uint8_t value) {
    switch (addr - kBase) {
        case kKbcData:   emu_.Get<I8042Controller>().WriteData(value);    break;
        case kKbcStatus: emu_.Get<I8042Controller>().WriteCommand(value); break;
        case kPic1Cmd:  pic_[0].WriteCmd(value);  break;
        case kPic1Data: pic_[0].WriteData(value); break;
        case kPic2Cmd:  pic_[1].WriteCmd(value);  break;
        case kPic2Data: pic_[1].WriteData(value); break;
        case kElcr1:    elcr_[0] = value; break;
        case kElcr2:    elcr_[1] = value; break;
        default:        HaltUnsupportedAccess("WriteByte", addr, value);
    }
}

void AliM1535::SaveState(StateWriter& w) {
    for (const Pic8259& p : pic_) {
        w.Write(p.imr); w.Write(p.irr); w.Write(p.isr);
        w.Write(p.icw1); w.Write(p.icw2); w.Write(p.icw3); w.Write(p.icw4);
        w.Write(p.init_step);
        w.Write(static_cast<uint8_t>(p.icw4_needed ? 1u : 0u));
        w.Write(static_cast<uint8_t>(p.single ? 1u : 0u));
        w.Write(static_cast<uint8_t>(p.read_isr ? 1u : 0u));
    }
    w.WriteBytes(elcr_, sizeof(elcr_));
    emu_.Get<I8042Controller>().SaveState(w);   /* delegated sub-block (not auto-enumerated) */
}

void AliM1535::RestoreState(StateReader& r) {
    for (Pic8259& p : pic_) {
        r.Read(p.imr); r.Read(p.irr); r.Read(p.isr);
        r.Read(p.icw1); r.Read(p.icw2); r.Read(p.icw3); r.Read(p.icw4);
        r.Read(p.init_step);
        uint8_t b = 0;
        r.Read(b); p.icw4_needed = b != 0;
        r.Read(b); p.single = b != 0;
        r.Read(b); p.read_isr = b != 0;
    }
    r.ReadBytes(elcr_, sizeof(elcr_));
    emu_.Get<I8042Controller>().RestoreState(r);
}

}  /* namespace */

REGISTER_SERVICE(AliM1535);
