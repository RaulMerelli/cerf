#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace {

/* Unpopulated static-bank space between the modem window and the
   CL-CD1284 debug board (SA-1110 Dev Manual ch.2 map). SA-1110 data
   aborts are MMU-generated, so these accesses complete: writes vanish,
   reads float - jlime pushes its post-MMU-off stack here on real HW. */
class Jornada720OpenBus : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Jornada720;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x08400000u; }
    uint32_t MmioSize() const override { return 0x11C00000u; }

    uint8_t  ReadByte(uint32_t addr) override { Trace("r8", addr);  return 0xFFu; }
    uint16_t ReadHalf(uint32_t addr) override { Trace("r16", addr); return 0xFFFFu; }
    uint32_t ReadWord(uint32_t addr) override { Trace("r32", addr); return 0xFFFFFFFFu; }

    void WriteByte(uint32_t addr, uint8_t)  override { Trace("w8", addr); }
    void WriteHalf(uint32_t addr, uint16_t) override { Trace("w16", addr); }
    void WriteWord(uint32_t addr, uint32_t) override { Trace("w32", addr); }

private:
    /* Breadcrumb so open-bus floating can never silently absorb a
       missing-peripheral bug the way an unlogged sink would. Capped so a
       floating-access storm can't flood the log; ships in production. */
    void Trace(const char* op, uint32_t addr) {
        if (++accesses_ <= 16u) {
            LOG(Periph, "[J720OpenBus] %s 0x%08X (floating bus)\n", op, addr);
        }
    }

    uint32_t accesses_ = 0;
};

}  /* namespace */

REGISTER_SERVICE(Jornada720OpenBus);
