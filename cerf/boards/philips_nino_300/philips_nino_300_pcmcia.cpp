#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../socs/pr31x00/pr31x00_card_space.h"
#include "../../socs/pr31x00/pr31x00_io.h"
#include "../board_context.h"

namespace {

/* The buffer chip's INT pin reaches multi-function I/O pin 2, whose rising edge is
   Interrupt Status 3 bit 2: NetBSD's own attachment for this board gives the chip
   `irq1 98` and `irq3 98` (conf/TX3912:176) and `MAKEINTR(s, b) = s * 32 + ffs(b) - 1`
   (tx/tx39var.h:118), and nk.exe 0x9F4338E4 masks exactly ENABLE3 bit 2. */
constexpr uint32_t kIt8368IntMfioPin = 2;

class PhilipsNino300Pcmcia : public Service,
                             public PcmciaSlotHost,
                             public IteIt8368IntSink {
public:
    explicit PhilipsNino300Pcmcia(CerfEmulator& emu)
        : Service(emu), slot0_(emu, *this, L"CompactFlash slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsNino300;
    }

    void OnReady() override {
        emu_.Get<Pr31x00CardSpace>().ProvideSockets(&slot0_, nullptr);
        emu_.Get<IteIt8368>().SetSlot(&slot0_);
        emu_.Get<IteIt8368>().SetIntSink(this);
        emu_.Get<HostWidgetRegistry>().Register(&slot0_);
    }

    void OnIt8368IntLevel(bool asserted) override {
        emu_.Get<Pr31x00Io>().DriveMfioInput(kIt8368IntMfioPin, asserted);
    }

    void OnShutdown() override { slot0_.OnShutdown(); }

    /* PcmciaSlotHost. The card-detect and card-IRQ pins reach the SoC through the
       board's multi-function I/O, whose assignment is not established. An empty,
       unpowered socket never raises either. */
    void OnCardDetectChanged(PcmciaSlot&) override {
        LOG(Caution, "PhilipsNino300Pcmcia: card-detect pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        LOG(Caution, "PhilipsNino300Pcmcia: card IRQ pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        LOG(Caution, "PhilipsNino300Pcmcia: card IRQ pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

private:
    PcmciaSlot slot0_;
};

}  /* namespace */

REGISTER_SERVICE(PhilipsNino300Pcmcia);
