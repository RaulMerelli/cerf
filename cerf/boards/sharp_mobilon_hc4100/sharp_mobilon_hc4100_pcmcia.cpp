#include "../../core/cerf_emulator.h"
#include "../../core/service.h"
#include "../../host/host_widget_registry.h"
#include "../../peripherals/ite_it8368/ite_it8368.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../socs/pr31x00/pr31x00_card_space.h"
#include "../../socs/pr31x00/pr31x00_io.h"
#include "../board_context.h"

namespace {

/* The IT8368E INT pin reaches multi-function I/O pin 2: the nk.exe OAL interrupt
   dispatch table at unk_91002338 maps Interrupt Status 3 bit 2 to SYSINTR $10,
   which pcmcia.dll sub_1492478 registers through InterruptInitialize. */
constexpr uint32_t kIt8368IntMfioPin = 2;

class SharpMobilonHc4100Pcmcia : public Service,
                                 public PcmciaSlotHost,
                                 public IteIt8368IntSink {
public:
    explicit SharpMobilonHc4100Pcmcia(CerfEmulator& emu)
        : Service(emu), slot0_(emu, *this, L"PC Card slot") {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SharpMobilonHc4100;
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

REGISTER_SERVICE(SharpMobilonHc4100Pcmcia);
