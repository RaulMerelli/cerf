#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../core/service.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../socs/pr31x00/pr31x00_card_space.h"
#include "../board_context.h"

namespace {

/* CRDDET1 and CRDDET2 are the two card-detect pins of one 16-bit PC Card socket
   (it8368.c:419 treats either as "no card"), and only the Card 1 windows are ever
   mapped: nk.1.exe sub_9FB2B1E4 maps $6400_0000 and sub_9FB2B054 maps $0800_0000. */
class PhilipsVelo1Pcmcia : public Service, public PcmciaSlotHost {
public:
    explicit PhilipsVelo1Pcmcia(CerfEmulator& emu)
        : Service(emu), slot0_(emu, *this, L"PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::PhilipsVelo1;
    }

    /* nk.exe sub_9F40F688 writes CTRL = $0A (BYTESWAP|GLOBALEN) and pcmcia.dll
       sub_1F11940 only ever adds GLOBALEN and CARDEN, so INTTRIEN stays clear and
       IteIt8368::IntLevel never asserts; the chip's INT pin has no sink here. */
    void OnReady() override {
        emu_.Get<Pr31x00CardSpace>().ProvideSockets(&slot0_, nullptr);
        emu_.Get<IteIt8368>().SetSlot(&slot0_);
        emu_.Get<HostWidgetRegistry>().Register(&slot0_);
    }

    void OnShutdown() override { slot0_.OnShutdown(); }

    void OnCardDetectChanged(PcmciaSlot&) override {
        LOG(Caution, "PhilipsVelo1Pcmcia: card-detect pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    void OnCardIrqAsserted(PcmciaSlot&) override {
        LOG(Caution, "PhilipsVelo1Pcmcia: card IRQ pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    void OnCardIrqDeasserted(PcmciaSlot&) override {
        LOG(Caution, "PhilipsVelo1Pcmcia: card IRQ pin unmodeled\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

private:
    PcmciaSlot slot0_;
};

}  /* namespace */

REGISTER_SERVICE(PhilipsVelo1Pcmcia);
