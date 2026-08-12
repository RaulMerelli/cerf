#pragma once

#include <cstdint>

/* ARM ARM DDI 0406C.c Table B1-1, p. B1-1139: CPSR.M[4:0] mode encodings. */
namespace ArmMode {
constexpr uint32_t kUser       = 0x10;
constexpr uint32_t kFiq        = 0x11;
constexpr uint32_t kIrq        = 0x12;
constexpr uint32_t kSupervisor = 0x13;
constexpr uint32_t kMonitor    = 0x16;
constexpr uint32_t kAbort      = 0x17;
constexpr uint32_t kHyp        = 0x1A;
constexpr uint32_t kUndefined  = 0x1B;
constexpr uint32_t kSystem     = 0x1F;
}  /* namespace ArmMode */

/* ARM ARM DDI 0406C.c A2.3, p. A2-45: SP, LR and PC are R13, R14 and R15. */
namespace ArmGpr {
constexpr uint32_t kR13 = 13;
constexpr uint32_t kR14 = 14;
constexpr uint32_t kR15 = 15;
}  /* namespace ArmGpr */

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1148: "The CPSR and SPSR bit assignments
   are:" N[31] Z[30] C[29] V[28] Q[27] IT[1:0][26:25] J[24] Reserved
   RAZ/SBZP[23:20] GE[3:0][19:16] IT[7:2][15:10] E[9] A[8] I[7] F[6] T[5]
   M[4:0]. */
union ArmPsrFull {
    uint32_t word;
    struct {
        uint32_t mode                 : 5;
        uint32_t thumb_mode           : 1;
        uint32_t fiq_disable          : 1;
        uint32_t irq_disable          : 1;
        uint32_t async_abort_disable  : 1;
        uint32_t endian               : 1;
        uint32_t it_high              : 6;
        uint32_t ge                   : 4;
        uint32_t reserved             : 4;
        uint32_t jazelle              : 1;
        uint32_t it_low               : 2;
        uint32_t saturation           : 1;
        uint32_t overflow             : 1;
        uint32_t carry                : 1;
        uint32_t zero                 : 1;
        uint32_t negative             : 1;
    } bits;
};

/* ARM ARM DDI 0406C.c B1.3.2, p. B1-1145: RBankSelect() maps '11111' System to
   the usr bank, and LookUpRName() passes RName_LRusr as the hyp argument for
   R14. Table B1-1, p. B1-1139: Monitor and Hyp are extension-only, every other
   mode is "Always". */
enum class ArmBank : uint32_t {
    kUsr, kFiq, kIrq, kSvc, kAbt, kUnd, kCount
};

/* ARM ARM DDI 0406C.c B1.3.3, p. B1-1152: SPSR[] resolves only FIQ, IRQ,
   Supervisor, Monitor, Abort, Hyp and Undefined; every other CPSR.M is
   "otherwise UNPREDICTABLE", so User and System have no SPSR. */
enum class ArmSpsrBank : uint32_t {
    kFiq, kIrq, kSvc, kAbt, kUnd, kCount
};

struct ArmCpuState {
    uint32_t    gprs[16];
    ArmPsrFull  cpsr{ArmMode::kSupervisor};

    uint32_t    sp_bank[static_cast<uint32_t>(ArmBank::kCount)];
    uint32_t    lr_bank[static_cast<uint32_t>(ArmBank::kCount)];
    ArmPsrFull  spsr_bank[static_cast<uint32_t>(ArmSpsrBank::kCount)];

    /* ARM ARM DDI 0406C.c Figure B1-2, p. B1-1144: "In addition FIQ mode has
       Banked copies of the ARM core registers R8 to R12". */
    uint32_t    r8_r12_fiq[5];

    uint64_t    vfp_d[32];
    uint32_t    fpscr;
    uint32_t    fpexc;

    uint64_t    acc0;

    uint32_t    ldrex_monitor_addr;
    uint32_t    ldrex_monitor_armed;

    uint32_t    guest_cycle_counter;
    uint32_t    irq_interrupt_pending;
    uint32_t    reset_pending;
    uint32_t    deep_sleep;

    /* QEMU hw/core/cpu-common.c:76 cpu_exit(), include/hw/core/cpu.h:504
       CPUState::exit_request. */
    uint32_t    chain_exit_request;
};

constexpr uint32_t kChainExitIrq   = 1u << 0;
constexpr uint32_t kChainExitReset = 1u << 1;
constexpr uint32_t kChainExitHost  = 1u << 2;
