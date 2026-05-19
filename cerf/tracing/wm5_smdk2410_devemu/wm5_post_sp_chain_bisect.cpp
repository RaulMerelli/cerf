#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

/* First-fire-only logger; subsequent fires bump a per-PC counter without
   logging. Tagged so we can grep [BISECT_FIRST] in the log to see
   "callee was reached at least once" vs "never". */
template <int Id>
struct FirstFire {
    static int count;
};
template <int Id> int FirstFire<Id>::count = 0;

#define FIRST_FIRE_LOG(id, fmt, ...) do {                                      \
    if (FirstFire<id>::count++ == 0) {                                         \
        LOG(Trace, "[BISECT_FIRST id=%d] " fmt, id, ##__VA_ARGS__);            \
    }                                                                          \
} while (0)

class TraceWm5PostSpChainBisect : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            /* Outer chain (sub_8007DDBC body). */
            tm.OnPc(0x8007B15Cu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(0, "sub_8007B15C ENTRY (kernel init; prints Sp=) LR=0x%08X\n",
                    c.regs[14]);
            });
            tm.OnPc(0x800771E8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(1, "@0x800771E8 sub_8007B15C RETURNED R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x8007DDBCu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(2, "sub_8007DDBC ENTRY LR=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x8009D0E8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(3, "sub_8009D0E8 ENTRY LR=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x800A1DF8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(4, "sub_800A1DF8 ENTRY LR=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x80088360u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(5, "sub_80088360 ENTRY R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x8008A904u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(6, "sub_8008A904 ENTRY R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x8007B36Cu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(7, "sub_8007B36C ENTRY (scheduler loop) R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x800B2098u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(8, "sub_800B2098 OEMIoControl(code=0x%08X) LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* Indirect dispatch in OEMIoControl: MOV LR,PC ; BX R4. R4 = v14[2] (table fn ptr). */
            tm.OnPc(0x800B21ACu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(30, "@0x800B21AC OEMIoControl pre-BX R4 (fn_ptr)=0x%08X R0(code)=0x%08X\n",
                    c.regs[4], c.regs[0]);
            });
            tm.OnPc(0x800B21B4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(31, "@0x800B21B4 OEMIoControl post-BX R4 returned R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            /* Dispatched callee for code 0x01010004 — OALIoCtlHalGetDeviceInfo. */
            tm.OnPc(0x800B28B0u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(32, "sub_800B28B0 OALIoCtlHalGetDeviceInfo ENTRY R0(code)=0x%08X R1(in_ptr)=0x%08X R2(in_size)=0x%08X R3(out_ptr)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3], c.regs[14]);
            });
            tm.OnPc(0x800B2A70u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(33, "@0x800B2A70 sub_800B28B0 pre-POP (about to return) R0=0x%08X\n", c.regs[0]);
            });
            tm.OnPc(0x800B2A74u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(34, "@0x800B2A74 sub_800B28B0 BX LR (returning) R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* Callees inside sub_800B28B0. */
            tm.OnPc(0x800A1704u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(35, "sub_800A1704 ENTRY (debug printf) LR=0x%08X\n", c.regs[14]);
            });
            tm.OnPc(0x800A84D4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(36, "sub_800A84D4 ENTRY R0=0x%08X LR=0x%08X\n", c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x800B3660u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(37, "sub_800B3660 ENTRY (memcpy) R0(dst)=0x%08X R1(src)=0x%08X R2(n)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
            });
            tm.OnPc(0x8008B808u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(38, "sub_8008B808 ENTRY (set last error) R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* Critical-section enter/leave referenced in OEMIoControl. */
            tm.OnPc(0x800A82B0u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(39, "sub_800A82B0 ENTRY (cs enter?) R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x800A8330u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(40, "sub_800A8330 ENTRY (cs leave?) R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* sub_800B2098 (OEMIoControl) callers. LR captured at entry will tell us
               which one. Hook return-PC after each BL sub_800B2098 to see which body
               continues. */
            tm.OnPc(0x8007E5C4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(50, "@0x8007E5C4 sub_8007E4DC post-BL sub_800B2098 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x8008A72Cu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(51, "@0x8008A72C post-BL sub_800B2098 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x800A4CC8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(52, "@0x800A4CC8 post-BL sub_800B2098 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x800A83F4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(53, "@0x800A83F4 post-BL sub_800B2098 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x800A842Cu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(54, "@0x800A842C post-BL sub_800B2098 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });
            /* sub_8007E4DC (wrapper) exit. */
            tm.OnPc(0x8007E5F4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(55, "@0x8007E5F4 sub_8007E4DC BX LR R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x8007E4DCu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(56, "sub_8007E4DC ENTRY R0(code)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* sub_800A4BAC = kernel API dispatcher (case 5 -> sub_800B2098).
               These hooks log EVERY fire — case 2 path runs first and returns
               fine; we need to see case 5's entry/exit too. */
            tm.OnPc(0x800A4BACu, [](const TraceContext& c) {
                LOG(Trace, "[BISECT_ALL] sub_800A4BAC ENTRY R0(case)=0x%08X R1=0x%08X SP=0x%08X LR(caller)=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[13], c.regs[14]);
                if (c.regs[0] == 5u) {
                    /* case=5 dispatches to OEMIoControl; LR is in .data region
                       per IDA static view. Dump runtime bytes at LR-16..LR+32
                       to see what code actually runs there. */
                    const uint32_t base = c.regs[14] - 16u;
                    LOG(Trace, "[BISECT_DUMP] runtime bytes @ VA 0x%08X..0x%08X:\n", base, base + 48u);
                    for (uint32_t off = 0; off < 48; off += 4) {
                        auto w = c.ReadVa32(base + off);
                        LOG(Trace, "[BISECT_DUMP]   +0x%02X (VA 0x%08X) = 0x%08X\n",
                            off, base + off, w ? *w : 0xDEADBEEFu);
                    }
                }
            });
            tm.OnPc(0x800A4D68u, [](const TraceContext& c) {
                LOG(Trace, "[BISECT_ALL] @0x800A4D68 sub_800A4BAC epilogue START SP=0x%08X\n",
                    c.regs[13]);
            });
            tm.OnPc(0x800A4D6Cu, [](const TraceContext& c) {
                LOG(Trace, "[BISECT_ALL] @0x800A4D6C sub_800A4BAC pre-POP {R4-R6,LR} SP=0x%08X\n",
                    c.regs[13]);
            });
            tm.OnPc(0x800A4D70u, [](const TraceContext& c) {
                LOG(Trace, "[BISECT_ALL] @0x800A4D70 sub_800A4BAC BX LR R0=0x%08X SP=0x%08X LR(=returnPC)=0x%08X\n",
                    c.regs[0], c.regs[13], c.regs[14]);
            });
            /* sub_800A4758 case=15 — LDR R3, [0x814A6320 + 4] ; MOV LR,PC ; BX R3.
               Hook just before BX R3 to log the indirect-call target. Also peek
               runtime [0x814A6320] and [0x814A6324] to see what's installed. */
            /* Kernel PFA handler — PSL syscall dispatch entry.
               Hook before and after the MSR CPSR_c #0x1F mode-switch
               at 0x800772B4 to confirm Abort→System bank-switch works. */
            tm.OnPc(0x8007729Cu, [](const TraceContext& c) {
                LOG(Trace, "[PFA_HANDLER_ENTRY] cpsr=0x%08X LR_abt=0x%08X "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R8=0x%08X R12=0x%08X SP=0x%08X\n",
                    c.cpsr, c.regs[14],
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[8], c.regs[12], c.regs[13]);
            });
            tm.OnPc(0x800772B4u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_PRE_MSR] cpsr=0x%08X (Abort mode) "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R8=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X\n",
                    c.cpsr,
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[8], c.regs[12], c.regs[13], c.regs[14]);
            });
            tm.OnPc(0x800772B8u, [](const TraceContext& c) {
                LOG(Trace, "[PFA_POST_MSR] cpsr=0x%08X (should be SYS=0x1F) "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R8=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X\n",
                    c.cpsr,
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[8], c.regs[12], c.regs[13], c.regs[14]);
            });
            /* Non-PSL abort path (LR >= 0x10400 after the F0000004 subtract). */
            tm.OnPc(0x8007756Cu, [](const TraceContext& c) {
                LOG(Trace, "[PFA_NON_PSL] cpsr=0x%08X R0=0x%08X LR=0x%08X "
                           "(non-PSL abort, fault outside trap range)\n",
                    c.cpsr, c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x80077424u, [](const TraceContext& c) {
                LOG(Trace, "[PSL_RET_BXNE] R12(user_PC)=0x%08X R0(ret)=0x%08X "
                           "R2(saved_mode)=0x%08X R3(stk_ptr)=0x%08X cpsr=0x%08X\n",
                    c.regs[12], c.regs[0], c.regs[2], c.regs[3], c.cpsr);
            });
            tm.OnPc(0x80077458u, [](const TraceContext& c) {
                LOG(Trace, "[PSL_RET_MOVS] R12(user_PC)=0x%08X R0(ret)=0x%08X "
                           "cpsr=0x%08X\n",
                    c.regs[12], c.regs[0], c.cpsr);
            });
            /* sub_8007A318 — kernel helper that resolves the user-mode return PC.
               Its return value lands in R12, which is what MOVS PC, R12 restores. */
            tm.OnPc(0x8007A318u, [](const TraceContext& c) {
                LOG(Trace, "[A318_ENTRY] sub_8007A318 R0(arg)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x3FA5998u, [](const TraceContext& c) {
                LOG(Trace, "[UDIV64_ENTRY] caller_LR=0x%08X dividend=0x%08X:%08X "
                           "divisor=0x%08X:%08X\n",
                    c.regs[14],
                    c.regs[1], c.regs[0],   /* a1 = (R1:R0) high:low */
                    c.regs[3], c.regs[2]);  /* a2 = (R3:R2) high:low */
            });
            tm.OnPc(0x3FA59A4u, [](const TraceContext& c) {
                LOG(Trace, "[UDIV64_BY_ZERO] caller_LR (from divide caller)=0x%08X "
                           "dividend=0x%08X:%08X (divisor was 0)\n",
                    c.regs[14],
                    c.regs[1], c.regs[0]);
            });
            /* sub_3FC1310 — coredll's RaiseException(STATUS_INTEGER_DIVIDE_BY_ZERO)
               wrapper. Reference fires 4 times. */
            tm.OnPc(0x3FC1310u, [](const TraceContext& c) {
                LOG(Trace, "[RAISE_DIVZERO] sub_3FC1310 LR=0x%08X\n", c.regs[14]);
            });
            /* amdnord.dll DSK_Init chain — every function in the path
               that ends at the Data Abort at AM29LV800_Init+0x58.
               Hook each entry + record args and LR. */
            tm.OnPc(0x3DF2474u, [](const TraceContext& c) {
                LOG(Trace, "[AMDNORD] DSK_Init ENTRY R0(reg_path)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x3DF6608u, [](const TraceContext& c) {
                LOG(Trace, "[AMDNORD] sub_3DF6608 ENTRY R0=0x%08X R1=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[14]);
            });
            tm.OnPc(0x3DF66F4u, [](const TraceContext& c) {
                LOG(Trace, "[AMDNORD] @0x3DF66F4 pre-BL AM29LV800_Init R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x3DF67E8u, [](const TraceContext& c) {
                LOG(Trace, "[AMDNORD] sub_3DF67E8 AM29LV800_Init ENTRY R0(flash_base)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            /* Last instruction before the fault. */
            tm.OnPc(0x3DF683Cu, [](const TraceContext& c) {
                LOG(Trace, "[AMDNORD] @0x3DF683C STRH R3,[R6,#0xAA] (pre-fault) "
                           "R3=0x%08X R6=0x%08X R7=0x%08X cpsr=0x%08X\n",
                    c.regs[3], c.regs[6], c.regs[7], c.cpsr);
            });
            tm.OnPc(0x3DF6840u, [](const TraceContext& c) {
                auto insn = c.ReadVa32(0x3DF6840u);
                LOG(Trace, "[AMDNORD] @0x3DF6840 LDRH R1,[R7] (FAULTING insn) "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X "
                           "SP=0x%08X LR=0x%08X cpsr=0x%08X "
                           "insn_bytes=0x%08X (expected E1D710B0)\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                    c.regs[13], c.regs[14], c.cpsr,
                    insn ? *insn : 0xDEADBEEFu);
            });
            /* AM29LV800_Init body — every unconditional PC (cond=AL only;
               conditional ARM insns drop their trace per debugging.md).
               Hook each insn with full GPR dump + insn bytes. */
            {
                static const uint32_t kAmdInitPcs[] = {
                    0x3DF67E8u, 0x3DF67ECu, 0x3DF67F0u, 0x3DF67F4u,
                    0x3DF67F8u, 0x3DF67FCu, 0x3DF6808u, 0x3DF680Cu,
                    0x3DF6810u, 0x3DF6814u, 0x3DF6818u, 0x3DF681Cu,
                    0x3DF6820u, 0x3DF6824u, 0x3DF6828u, 0x3DF682Cu,
                    0x3DF6830u, 0x3DF6834u, 0x3DF6838u, 0x3DF6844u,
                    0x3DF6848u, 0x3DF684Cu, 0x3DF6850u, 0x3DF6878u,
                    0x3DF687Cu, 0x3DF6888u, 0x3DF688Cu, 0x3DF6890u,
                };
                for (uint32_t pc : kAmdInitPcs) {
                    tm.OnPc(pc, [pc](const TraceContext& c) {
                        auto insn = c.ReadVa32(pc);
                        LOG(Trace, "[AMD_STEP] PC=0x%08X insn=0x%08X "
                                   "R0=%08X R1=%08X R2=%08X R3=%08X "
                                   "R4=%08X R5=%08X R6=%08X R7=%08X "
                                   "SP=%08X LR=%08X\n",
                            pc, insn ? *insn : 0xDEADBEEFu,
                            c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                            c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                            c.regs[13], c.regs[14]);
                    });
                }
            }
            /* coredll.dll CaptureDumpFileOnDevice (0x3F7DBB8). Reference
               fires 2 times. */
            tm.OnPc(0x3F7DBB8u, [](const TraceContext& c) {
                LOG(Trace, "[CAPTURE_DUMP] CaptureDumpFileOnDevice LR=0x%08X "
                           "R0=0x%08X R1=0x%08X R2=0x%08X\n",
                    c.regs[14], c.regs[0], c.regs[1], c.regs[2]);
            });
            /* Kernel runtime-generated trampoline at 0x800BEAB0+ — what runs
               after sub_800A4BAC(case=5, OEMIoControl) returns success.
               Hook every-4-bytes window plus dump runtime bytes once. */
            for (uint32_t pc = 0x800BEAB0u; pc <= 0x800BEBE0u; pc += 4) {
                tm.OnPc(pc, [pc](const TraceContext& c) {
                    LOG(Trace, "[POST_OEMIOCTL] PC=0x%08X R0=0x%08X R1=0x%08X "
                               "R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X "
                               "R7=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X cpsr=0x%08X\n",
                        pc,
                        c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                        c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                        c.regs[12], c.regs[13], c.regs[14], c.cpsr);
                });
            }
            /* Dump runtime bytes 0x800BEAB0..0x800BEBE0 once on first
               sub_800A4BAC case=5 entry. */
            tm.OnPc(0x800BEA84u, [](const TraceContext& c) {
                static int n = 0;
                if (n++ != 0) return;
                LOG(Trace, "[POST_OEMIOCTL_DUMP] runtime bytes 0x800BEAB0..0x800BEBE0:\n");
                for (uint32_t off = 0; off <= 0x130; off += 16) {
                    auto v0 = c.ReadVa32(0x800BEAB0u + off);
                    auto v1 = c.ReadVa32(0x800BEAB0u + off + 4);
                    auto v2 = c.ReadVa32(0x800BEAB0u + off + 8);
                    auto v3 = c.ReadVa32(0x800BEAB0u + off + 12);
                    LOG(Trace, "[POST_OEMIOCTL_DUMP] +0x%03X (0x%08X): %08X %08X %08X %08X\n",
                        off, 0x800BEAB0u + off,
                        v0 ? *v0 : 0xDEADBEEFu, v1 ? *v1 : 0xDEADBEEFu,
                        v2 ? *v2 : 0xDEADBEEFu, v3 ? *v3 : 0xDEADBEEFu);
                }
            });
            /* Kernel SWI handler (offset 0x3E8). */
            tm.OnPc(0x80077270u, [](const TraceContext& c) {
                LOG(Trace, "[SWI_HANDLER] sub_80077270 ENTRY R0=0x%08X cpsr=0x%08X\n",
                    c.regs[0], c.cpsr);
            });

            tm.OnPc(0x800A4A30u, [](const TraceContext& c) {
                auto base   = c.ReadVa32(0x814A6320u);
                auto slot_4 = c.ReadVa32(0x814A6324u);
                LOG(Trace, "[CASE15_DISPATCH] sub_800A4758 BX R3 target=0x%08X "
                           "(MEMORY[0x814A6324]) R0=0x%08X R1=0x%08X R2=0x%08X "
                           "runtime[0x814A6320]=0x%08X runtime[0x814A6324]=0x%08X\n",
                    c.regs[3], c.regs[0], c.regs[1], c.regs[2],
                    base   ? *base   : 0xDEADBEEFu,
                    slot_4 ? *slot_4 : 0xDEADBEEFu);
            });
            tm.OnPc(0x8007B8A4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(9, "sub_8007B8A4 EXCEPTION_DISPATCHER R0(ctx)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });

            /* Inside sub_800A1DF8: one-shot pre-loop call, then loop body. */
            tm.OnPc(0x800B02F4u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(10, "sub_800B02F4 ENTRY LR=0x%08X\n", c.regs[14]);
            });

            /* Outer loop iter entry (0x800A1F40). */
            tm.OnPc(0x800A1F40u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(11, "@0x800A1F40 outer-loop iter (R4=region=0x%08X)\n",
                    c.regs[4]);
            });

            /* Inner loop callees + their return-PCs. */
            tm.OnPc(0x800A1F64u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(12, "@0x800A1F64 pre-BL sub_8007C1C8 R0(VA)=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x8007C1C8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(13, "sub_8007C1C8 ENTRY R0(VA)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x800A1F68u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(14, "@0x800A1F68 post-BL sub_8007C1C8 R0(rc)=0x%08X\n",
                    c.regs[0]);
            });

            tm.OnPc(0x800A1FACu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(15, "@0x800A1FAC pre-BL sub_80077840 R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x80077840u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(16, "sub_80077840 ENTRY R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x800A1FB0u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(17, "@0x800A1FB0 post-BL sub_80077840 R0=0x%08X\n",
                    c.regs[0]);
            });

            tm.OnPc(0x800A1FB8u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(18, "@0x800A1FB8 pre-BL sub_800A1928 R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x800A1928u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(19, "sub_800A1928 ENTRY R0=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[14]);
            });
            tm.OnPc(0x800A1FBCu, [](const TraceContext& c) {
                FIRST_FIRE_LOG(20, "@0x800A1FBC post-BL sub_800A1928 R0=0x%08X\n",
                    c.regs[0]);
            });

            /* sub_800A1DF8 loop exit / return. */
            tm.OnPc(0x800A1FF0u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(21, "@0x800A1FF0 sub_800A1DF8 loop-exit (R4=0x%08X)\n",
                    c.regs[4]);
            });
            tm.OnPc(0x800A2008u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(22, "@0x800A2008 sub_800A1DF8 BX LR (returning) R0=0x%08X\n",
                    c.regs[0]);
            });
            tm.OnPc(0x8007DE48u, [](const TraceContext& c) {
                FIRST_FIRE_LOG(23, "@0x8007DE48 sub_800A1DF8 RETURNED to caller R0=0x%08X\n",
                    c.regs[0]);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5PostSpChainBisect);

}  /* namespace */
