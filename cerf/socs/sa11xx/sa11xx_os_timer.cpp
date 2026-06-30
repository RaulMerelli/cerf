#include "../os_timer.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_context.h"
#include "sa11xx_intc.h"

namespace {

/* SA-1110 §9.2.1.1 Table 9-1: IP26..29 = OSMR0..3 match. */
constexpr uint32_t kIntcOst0Bit = 26u;

class Sa11xxOsTimer : public OsTimer {
public:
    using OsTimer::OsTimer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSoc() == SocFamily::SA1110 || bd->GetSoc() == SocFamily::SA1100);
    }

    uint32_t MmioBase() const override { return 0x90000000u; }

protected:
    void SetMatchLevel(uint32_t level4) override {
        emu_.Get<Sa11xxIntc>().SetSourceLevel(0xFu << kIntcOst0Bit,
                                              (level4 & 0xFu) << kIntcOst0Bit);
    }
};

REGISTER_SERVICE(Sa11xxOsTimer);

}  /* namespace */
