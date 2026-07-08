#include "vr4102_icu.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../jit/mips/mips_jit.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <cstdint>

namespace {

/* ICU1 block offsets from 0x0B000080 (UM Table 14-1). */
enum : uint32_t {
    kSysint1 = 0x00, kPiuint = 0x02, kAiuint = 0x04, kKiuint = 0x06,
    kGiuintl = 0x08, kDsiuint = 0x0A, kMsysint1 = 0x0C, kMpiu = 0x0E,
    kMaiu = 0x10, kMkiu = 0x12, kMgiul = 0x14, kMdsiu = 0x16,
    kNmireg = 0x18, kSoftint = 0x1A,
};
/* ICU2 block offsets from 0x0B000200 (UM Table 14-1). */
enum : uint32_t {
    kSysint2 = 0x00, kGiuinth = 0x02, kFirint = 0x04,
    kMsysint2 = 0x06, kMgiuh = 0x08, kMfir = 0x0A,
};

/* SYSINT1REG bit positions (UM p295) that route to a dedicated CPU line or need
   special handling; SYSINT2REG (UM p311); NMIREG (UM p309). */
constexpr uint16_t kS1Bat   = 1u << 0;   /* BATINTR  -> NMI or Int0 (NMIREG) */
constexpr uint16_t kS1RtcL1 = 1u << 2;   /* RTCL1INTR -> Int1 */
constexpr uint16_t kS2RtcL2 = 1u << 0;   /* RTCL2INTR -> Int2 */
constexpr uint16_t kS2Hsp   = 1u << 2;   /* HSPINTR   -> Int3 */
constexpr uint16_t kNmiSel  = 1u << 0;   /* NMIREG.NMIORINT: 1=Int0, 0=NMI */

/* SYSINT direct-source bits (no Level-2 register): SYSINT1 {BAT,POWER,RTCL1,
   ETIMER,SIU,WRBERR,DOZEPIU} = D0,1,2,3,9,10,13; SYSINT2 {RTCL2,LED,HSP,TCLK}
   = D0,1,2,3. The collapsed bits (PIU/AIU/KIU/GIU/SOFT, DSIU/FIR) are computed. */
constexpr uint16_t kS1DirectMask = 0x260Fu;
constexpr uint16_t kS2DirectMask = 0x000Fu;

/* Int0..Int3 map to MIPS Cause.IP2..IP5 (bits 10..13). */
constexpr uint32_t kIp2 = 1u << 10;  /* Int0 */
constexpr uint32_t kIp3 = 1u << 11;  /* Int1 */
constexpr uint32_t kIp4 = 1u << 12;  /* Int2 */
constexpr uint32_t kIp5 = 1u << 13;  /* Int3 */

}  // namespace

REGISTER_SERVICE(Vr4102Icu);

bool Vr4102Icu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::VR4102;
}

void Vr4102Icu::OnReady() { emu_.Get<PeripheralDispatcher>().Register(this); }

uint16_t Vr4102Icu::ComputeSysint1Locked() const {
    uint16_t s = sysint1_direct_ & kS1DirectMask;
    if (piuint_ & mpiu_) s |= (1u << 5);   /* PIUINTR */
    if (aiuint_ & maiu_) s |= (1u << 6);   /* AIUINTR */
    if (kiuint_ & mkiu_) s |= (1u << 7);   /* KIUINTR */
    if ((giuintl_ & mgiul_) | (giuinth_ & mgiuh_)) s |= (1u << 8);  /* GIUINTR */
    if (softint_ & 0x000Fu) s |= (1u << 11);  /* SOFTINTR */
    return s;
}

uint16_t Vr4102Icu::ComputeSysint2Locked() const {
    uint16_t s = sysint2_direct_ & kS2DirectMask;
    if (firint_ & mfir_) s |= (1u << 4);    /* FIRINTR */
    if (dsiuint_ & mdsiu_) s |= (1u << 5);  /* DSIUINTR */
    return s;
}

void Vr4102Icu::RecomputeLocked() {
    const uint16_t m1 = ComputeSysint1Locked() & msysint1_;
    const uint16_t m2 = ComputeSysint2Locked() & msysint2_;

    uint32_t ip = 0;
    if (m1 & kS1RtcL1) ip |= kIp3;   /* rtclong1 -> Int1 */
    if (m2 & kS2RtcL2) ip |= kIp4;   /* rtclong2 -> Int2 */
    if (m2 & kS2Hsp)   ip |= kIp5;   /* hsp -> Int3 */

    const bool bat = (m1 & kS1Bat) != 0;
    if (bat && !(nmireg_ & kNmiSel)) {
        /* battint routed to NMI (NMIREG.NMIORINT=0): MIPS NMI delivery is not
           modeled, so this is born-fatal until a battery source + an NMI path
           exist. Unreachable at present - no source asserts BATINTR. */
        LOG(Caution, "Vr4102Icu: battint routed to NMI (NMIREG=0x%04X) but NMI "
                     "delivery is not modeled\n", nmireg_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    /* Int0 = every masked interrupt except battint (-> NMI/Int0) and the ones on
       dedicated lines (rtclong1 -> Int1, rtclong2 -> Int2, hsp -> Int3). battint
       joins Int0 only when NMIREG selects Int0. (UM Fig 14-1.) */
    const uint16_t int0_1 = m1 & ~static_cast<uint16_t>(kS1Bat | kS1RtcL1);
    const uint16_t int0_2 = m2 & ~static_cast<uint16_t>(kS2RtcL2 | kS2Hsp);
    const bool bat_int0 = bat && (nmireg_ & kNmiSel);
    if (int0_1 || int0_2 || bat_int0) ip |= kIp2;

    emu_.Get<MipsJit>().SetExternalInterruptLevel(ip);
}

uint16_t Vr4102Icu::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kSysint1:  return ComputeSysint1Locked();
        case kPiuint:   return piuint_;
        case kAiuint:   return aiuint_;
        case kKiuint:   return kiuint_;
        case kGiuintl:  return giuintl_;
        case kDsiuint:  return dsiuint_;
        case kMsysint1: return msysint1_;
        case kMpiu:     return mpiu_;
        case kMaiu:     return maiu_;
        case kMkiu:     return mkiu_;
        case kMgiul:    return mgiul_;
        case kMdsiu:    return mdsiu_;
        case kNmireg:   return nmireg_;
        case kSoftint:  return softint_;
        default:        HaltUnsupportedAccess("ICU ReadHalf", addr, 0);
    }
}

void Vr4102Icu::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - MmioBase();
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        /* SYSINT1REG + the Level-2 indication registers are read-only (UM Table
           14-1); a write is discarded by the silicon. */
        case kSysint1: case kPiuint: case kAiuint: case kKiuint:
        case kGiuintl: case kDsiuint: return;
        case kMsysint1: msysint1_ = value; RecomputeLocked(); return;
        case kMpiu:     mpiu_ = value;     RecomputeLocked(); return;
        case kMaiu:     maiu_ = value;     RecomputeLocked(); return;
        case kMkiu:     mkiu_ = value;     RecomputeLocked(); return;
        case kMgiul:    mgiul_ = value;    RecomputeLocked(); return;
        case kMdsiu:    mdsiu_ = value;    RecomputeLocked(); return;
        case kNmireg:   nmireg_ = value & kNmiSel; RecomputeLocked(); return;
        case kSoftint:  softint_ = value & 0x000Fu; RecomputeLocked(); return;
        default:        HaltUnsupportedAccess("ICU WriteHalf", addr, value);
    }
}

uint16_t Vr4102Icu::ReadHalf2(uint32_t off) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kSysint2:  return ComputeSysint2Locked();
        case kGiuinth:  return giuinth_;
        case kFirint:   return firint_;
        case kMsysint2: return msysint2_;
        case kMgiuh:    return mgiuh_;
        case kMfir:     return mfir_;
        default:        HaltUnsupportedAccess("ICU2 ReadHalf", 0x0B000200u + off, 0);
    }
}

void Vr4102Icu::WriteHalf2(uint32_t off, uint16_t value) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kSysint2: case kGiuinth: case kFirint: return;  /* read-only (Table 14-1) */
        case kMsysint2: msysint2_ = value; RecomputeLocked(); return;
        case kMgiuh:    mgiuh_ = value;    RecomputeLocked(); return;
        case kMfir:     mfir_ = value;     RecomputeLocked(); return;
        default:        HaltUnsupportedAccess("ICU2 WriteHalf", 0x0B000200u + off, value);
    }
}

void Vr4102Icu::SetSysint1Source(uint16_t bit, bool level) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (level) sysint1_direct_ |= bit; else sysint1_direct_ &= ~bit;
    RecomputeLocked();
}
void Vr4102Icu::SetSysint2Source(uint16_t bit, bool level) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (level) sysint2_direct_ |= bit; else sysint2_direct_ &= ~bit;
    RecomputeLocked();
}
void Vr4102Icu::SetGiuLow(uint16_t bits) {
    std::lock_guard<std::mutex> lk(mtx_);
    giuintl_ = bits;
    RecomputeLocked();
}
void Vr4102Icu::SetGiuHigh(uint16_t bits) {
    std::lock_guard<std::mutex> lk(mtx_);
    giuinth_ = bits;
    RecomputeLocked();
}
void Vr4102Icu::SetPiuSource(uint16_t bits) {
    std::lock_guard<std::mutex> lk(mtx_);
    piuint_ = bits;
    RecomputeLocked();
}
void Vr4102Icu::SetKiuSource(uint16_t bits) {
    std::lock_guard<std::mutex> lk(mtx_);
    kiuint_ = bits;
    RecomputeLocked();
}

void Vr4102Icu::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write(giuintl_); w.Write(giuinth_); w.Write(piuint_); w.Write(aiuint_);
    w.Write(kiuint_); w.Write(dsiuint_); w.Write(firint_);
    w.Write(mgiul_); w.Write(mgiuh_); w.Write(mpiu_); w.Write(maiu_);
    w.Write(mkiu_); w.Write(mdsiu_); w.Write(mfir_);
    w.Write(sysint1_direct_); w.Write(sysint2_direct_);
    w.Write(msysint1_); w.Write(msysint2_); w.Write(nmireg_); w.Write(softint_);
}

void Vr4102Icu::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read(giuintl_); r.Read(giuinth_); r.Read(piuint_); r.Read(aiuint_);
    r.Read(kiuint_); r.Read(dsiuint_); r.Read(firint_);
    r.Read(mgiul_); r.Read(mgiuh_); r.Read(mpiu_); r.Read(maiu_);
    r.Read(mkiu_); r.Read(mdsiu_); r.Read(mfir_);
    r.Read(sysint1_direct_); r.Read(sysint2_direct_);
    r.Read(msysint1_); r.Read(msysint2_); r.Read(nmireg_); r.Read(softint_);
}

void Vr4102Icu::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    RecomputeLocked();  /* re-drive the CPU Int level from restored state */
}
