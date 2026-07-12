#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../socs/pr31x00/pr31x00_card_space.h"
#include "../../socs/pr31x00/pr31x00_ir.h"
#include "../board_context.h"

namespace {

/* CRDDET1 and CRDDET2 are the two card-detect pins of one 16-bit PC Card socket
   (it8368.c:419 treats either as "no card"), and only the Card 1 windows are ever
   mapped: nk.1.exe sub_9FB2B1E4 maps $6400_0000 and sub_9FB2B054 maps $0800_0000. */
class PhilipsVelo1Pcmcia : public Service,
                           public PcmciaSlotHost,
                           public IteIt8368IntSink {
public:
    explicit PhilipsVelo1Pcmcia(CerfEmulator& emu)
        : Service(emu), slot0_(emu, *this, L"PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    void OnReady() override {
        emu_.Get<Pr31x00CardSpace>().ProvideSockets(&slot0_, nullptr);
        emu_.Get<IteIt8368>().SetSlot(&slot0_);
        emu_.Get<IteIt8368>().SetIntSink(this);
        emu_.Get<HostWidgetRegistry>().Register(&slot0_);
    }

    /* The buffer chip's INT pin reaches the SoC's CARDET input, whose rising edge is
       Interrupt Status 5 bit 15 POSCARINT (§8.3.5): pcmcia.dll arms exactly that bit,
       writing Clear5 = $8000 then Enable5 |= $8000 after powering the socket
       (sub_1F11940) and re-arming it around every event-mask change (sub_1F1178C). */
    void OnIt8368IntLevel(bool asserted) override {
        emu_.Get<Pr31x00Ir>().DriveCarDetInput(asserted);
    }

    void OnShutdown() override { slot0_.OnShutdown(); }

    void OnCardDetectChanged(PcmciaSlot&) override {
        emu_.Get<IteIt8368>().NotifyCardDetect();
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        emu_.Get<IteIt8368>().SetCardIrq(true);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        emu_.Get<IteIt8368>().SetCardIrq(false);
    }

private:
    PcmciaSlot slot0_;
};

}  /* namespace */

REGISTER_SERVICE(PhilipsVelo1Pcmcia);
