#include "../../peripherals/cirrus_pd6710/pd6710_card_irq_line.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

namespace {

constexpr int kPd6710CardEintNumber = 8;

class DevEmuPd6710CardIrqLine : public Pd6710CardIrqLine {
public:
    using Pd6710CardIrqLine::Pd6710CardIrqLine;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::Smdk2410DevEmu;
    }

    void Assert() override {
        LOG(Caution, "[PD6710] card IRQ assert EINT%d - no S3C2410 EINT sink; "
                "halting\n", kPd6710CardEintNumber);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    void Deassert() override {}
};

}

REGISTER_SERVICE_AS(DevEmuPd6710CardIrqLine, Pd6710CardIrqLine);
