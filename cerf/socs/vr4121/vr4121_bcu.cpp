#include "../../peripherals/peripheral_base.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../guest_cpu_reset.h"

#include <cstdint>

namespace {

/* VR4121 BCU (Bus Control Unit), Internal I/O Space 2 (UM Table 1-1). The DMAAU
   block follows at 0x0B000020 (UM Table 1-2), so the BCU decodes 0x0B000000-1F. */
constexpr uint32_t kBase = 0x0B000000u;
constexpr uint32_t kSize = 0x20u;

constexpr uint32_t kOffCntReg1  = 0x00u;   /* BCUCNTREG1  (UM 11.2.1)  */
constexpr uint32_t kOffErrStReg = 0x0Cu;   /* BCUERRSTREG (UM 11.2.6)  */
constexpr uint32_t kOffRfCntReg = 0x0Eu;   /* BCURFCNTREG (UM 11.2.7)  */
constexpr uint32_t kOffCntReg3  = 0x16u;   /* BCUCNTREG3  (UM 11.2.11) */

/* BCUCNTREG1 (UM 11.2.1): R/W bits D15/14/13/12/10/8/6/4/3/2/1/0, RFU-read-0 D11/9/7/5;
   RTCRST/After-reset 0 except D14 (Note 1 board DRAM strap, unmodeled). MMCRestore.exe
   0x12B7C RMWs it without branching (set D6 ROMWEN2, clear D10 PAGEROM2). */
constexpr uint16_t kCntReg1Writable = 0xF55Fu;
constexpr uint16_t kCntReg1RfuRead0 = 0x0AA0u;

/* BCUERRSTREG (UM 11.2.6): D0 BERRST, "Bus error status. Clear to 0 when 1 is
   written."; D15:1 RFU, "Write 0 to these bits. 0 is returned after a read." */
constexpr uint16_t kErrStBerrst = 0x0001u;

/* BCURFCNTREG (UM 11.2.7): D13:0 BRF(13:0), "Number of DRAM refresh cycles (with
   TClock cycle)"; D15:14 RFU read 0. RTCRST column = BRF9. */
constexpr uint16_t kRfCntBrf      = 0x3FFFu;
constexpr uint16_t kRfCntPowerOn  = 0x0200u;

/* BCUCNTREG3 (UM 11.2.11): D15/14/13/12/11/7 R/W; D2/1/0 also R/W - print "R" but
   UM 11.4.6 + kernel RMW 0x9F0B5B80 (`lhu;ori 7;sh`) write LCDSEL(1:0)/BSEL; D10:8/D6:3
   RFU read-0; RTCRST 0 except D14 (Note 1 SDRAM strap, unmodeled). */
constexpr uint16_t kCntReg3Writable = 0xF887u;
constexpr uint16_t kCntReg3RfuRead0 = 0x0778u;

class Vr4121Bcu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::VR4121;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        /* BCUERRSTREG (UM 11.2.6, p287) and BCUCNTREG1 (UM 11.2.1, p276) RTCRST and
           After-reset rows are both 0. BCURFCNTREG (UM 11.2.7, p288) and BCUCNTREG3
           (UM 11.2.11, p292) carry their RTCRST column only on an RTC reset; their
           After-reset rows are "Value before reset is retained". */
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            errstreg_ = 0;
            cntreg1_  = 0;
            if (kind != ResetLineKind::Rtc) return;
            rfcntreg_ = kRfCntPowerOn;
            cntreg3_  = 0;
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffCntReg1:  return cntreg1_;
            case kOffRfCntReg: return rfcntreg_;
            case kOffCntReg3:  return cntreg3_;
            default: return Peripheral::ReadHalf(addr);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffCntReg1:
                cntreg1_ = static_cast<uint16_t>(value & kCntReg1Writable);
                return;
            case kOffErrStReg:
                errstreg_ = static_cast<uint16_t>(errstreg_ & ~(value & kErrStBerrst));
                return;
            case kOffRfCntReg:
                rfcntreg_ = static_cast<uint16_t>(value & kRfCntBrf);
                return;
            case kOffCntReg3:
                cntreg3_ = static_cast<uint16_t>(value & kCntReg3Writable);
                return;
            default: Peripheral::WriteHalf(addr, value); return;
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write(cntreg1_);
        w.Write(errstreg_);
        w.Write(rfcntreg_);
        w.Write(cntreg3_);
    }

    void RestoreState(StateReader& r) override {
        r.Read(cntreg1_);
        r.Read(errstreg_);
        r.Read(rfcntreg_);
        r.Read(cntreg3_);
    }

private:
    uint16_t cntreg1_  = 0u;                 /* UM 11.2.1 RTCRST + After-reset rows, less the D14 strap */
    uint16_t errstreg_ = 0u;                 /* UM 11.2.6 RTCRST + After-reset rows */
    uint16_t rfcntreg_ = kRfCntPowerOn;      /* UM 11.2.7 RTCRST row */
    uint16_t cntreg3_  = 0u;                 /* UM 11.2.11 RTCRST row, less the D14 strap */

    static_assert((kCntReg1Writable & kCntReg1RfuRead0) == 0u,
                  "BCUCNTREG1 writable and read-0 RFU bits overlap");
    static_assert((kCntReg3Writable & kCntReg3RfuRead0) == 0u,
                  "BCUCNTREG3 writable and read-0 RFU bits overlap");
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Bcu);
