#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* S3C2410A UM pp. 21-5..21-8 "IIS-BUS INTERFACE SPECIAL REGISTERS": IISCON at
   0x55000000, IISMOD 0x55000004, IISPSR 0x55000008, IISFCON 0x5500000C,
   IISFIFO 0x55000010. */
constexpr uint32_t kBase = 0x55000000u;
constexpr uint32_t kSpan = 0x14u;

constexpr uint32_t kOffCon  = 0x00u;
constexpr uint32_t kOffMod  = 0x04u;
constexpr uint32_t kOffPsr  = 0x08u;
constexpr uint32_t kOffFcon = 0x0Cu;

/* UM p. 21-5 IISCON, reset 0x100: [8] left/right channel index and [7]/[6] the
   transmit/receive FIFO ready flags are Read only; [5:0] are writable, [0]
   being "IIS interface 0 = Disable (stop) 1 = Enable (start)". */
constexpr uint32_t kConWritable = 0x0000003Fu;
constexpr uint32_t kConReset    = 0x00000100u;
constexpr uint32_t kConEnable   = 1u << 0;

/* UM p. 21-6 IISMOD, reset 0x0: [8] master/slave mode select, [7:6] transmit/
   receive mode select, [5] active level of left/right channel, [4] serial
   interface format, [3] serial data bit per channel, [2] master clock
   frequency select, [1:0] serial bit clock frequency select. */
constexpr uint32_t kModWritable = 0x000001FFu;

/* UM p. 21-7 IISPSR, reset 0x0: [9:5] prescaler control A, [4:0] prescaler
   control B, each "Data value: 0 ~ 31" with division factor N+1. */
constexpr uint32_t kPsrWritable = 0x000003FFu;

/* UM p. 21-8 IISFCON, reset 0x0: [15] transmit FIFO access mode select,
   [14] receive FIFO access mode select, [13] transmit FIFO enable, [12]
   receive FIFO enable; [11:6] transmit and [5:0] receive FIFO data count are
   Read only, "Data count value = 0 ~ 32". */
constexpr uint32_t kFconWritable = 0x0000F000u;
constexpr uint32_t kFconTxEnable = 1u << 13;
constexpr uint32_t kFconRxEnable = 1u << 12;

class S3C2410Iis : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::S3C2410;
    }

    void OnReady() override {
        Reset();
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { Reset(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSpan; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>(con_);
        w.Write<uint32_t>(mod_);
        w.Write<uint32_t>(psr_);
        w.Write<uint32_t>(fcon_);
    }
    void RestoreState(StateReader& r) override {
        r.Read(con_);
        r.Read(mod_);
        r.Read(psr_);
        r.Read(fcon_);
    }

private:
    void Reset() {
        con_  = kConReset;
        mod_  = 0u;
        psr_  = 0u;
        fcon_ = 0u;
    }

    uint32_t con_  = kConReset;
    uint32_t mod_  = 0u;
    uint32_t psr_  = 0u;
    uint32_t fcon_ = 0u;
};

uint32_t S3C2410Iis::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint32_t value = 0;
    switch (off) {
        case kOffCon:  value = con_;  break;
        case kOffMod:  value = mod_;  break;
        case kOffPsr:  value = psr_;  break;
        case kOffFcon: value = fcon_; break;
        default:
            HaltUnsupportedAccess("ReadWord", addr, 0);
    }
#if CERF_DEV_MODE
    LOG(SocIis, "read  +0x%02X -> 0x%08X\n", off, value);
#endif
    return value;
}

void S3C2410Iis::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
#if CERF_DEV_MODE
    LOG(SocIis, "write +0x%02X = 0x%08X\n", off, value);
#endif
    switch (off) {
        case kOffCon:
            if ((value & kConEnable) != 0u) {
                HaltUnsupportedAccess("WriteWord IISCON interface enable",
                                      addr, value);
            }
            con_ = (con_ & ~kConWritable) | (value & kConWritable);
            return;
        case kOffMod:
            mod_ = value & kModWritable;
            return;
        case kOffPsr:
            psr_ = value & kPsrWritable;
            return;
        case kOffFcon:
            if ((value & (kFconTxEnable | kFconRxEnable)) != 0u) {
                HaltUnsupportedAccess("WriteWord IISFCON FIFO enable",
                                      addr, value);
            }
            fcon_ = value & kFconWritable;
            return;
        default:
            HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

}  /* namespace */

REGISTER_SERVICE(S3C2410Iis);
