#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "wm5_bundle.h"

namespace {

class TraceWm5PrintfHexLoop : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&tm] {
            tm.OnPc(0x800A0E74u, [](const TraceContext& c) {
                LOG(Trace, "[HEX_ENTRY] sub_800A0E74 "
                           "R0(cursor)=0x%08X R1(value)=0x%08X "
                           "R2(count)=0x%08X R3(base)=0x%08X "
                           "SP=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[13], c.regs[14]);
            });

            /* Start of loop body. R0 about to be overwritten with
               R9 (=original R3=base) for the divmod call; R1 holds
               the running dividend from the previous iteration's
               quotient. R5 is the remaining max-count. */
            tm.OnPc(0x800A0E98u, [](const TraceContext& c) {
                LOG(Trace, "[HEX_LOOP] @E98 "
                           "R0=0x%08X R1(dividend)=0x%08X "
                           "R5(count)=0x%08X R9(base)=0x%08X "
                           "R4(digits_so_far)=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[5],
                    c.regs[9], c.regs[4]);
            });

            /* After divmod in uppercase path (0x800A0EA8 BL returns
               here). R0 should be the quotient, R1 the remainder
               (= digit). */
            tm.OnPc(0x800A0EACu, [](const TraceContext& c) {
                LOG(Trace, "[HEX_POST_DIVMOD_UC] "
                           "R0(quotient)=0x%08X R1(remainder=digit)=0x%08X\n",
                    c.regs[0], c.regs[1]);
            });

            /* After divmod in lowercase path. */
            tm.OnPc(0x800A0EBCu, [](const TraceContext& c) {
                LOG(Trace, "[HEX_POST_DIVMOD_LC] "
                           "R0(quotient)=0x%08X R1(remainder=digit)=0x%08X\n",
                    c.regs[0], c.regs[1]);
            });

            /* STRH R2, [R7],#2 — about to write the digit char to
               the output buffer at R7, then post-inc R7 by 2. */
            tm.OnPc(0x800A0EC4u, [](const TraceContext& c) {
                LOG(Trace, "[HEX_STORE] "
                           "R7(cursor)=0x%08X R2(char)=0x%04X "
                           "R0(quotient)=0x%08X R4(digit_idx)=0x%08X\n",
                    c.regs[7], c.regs[2] & 0xFFFFu,
                    c.regs[0], c.regs[4]);
            });

            /* Loop-back check at 0x800A0ED4 — about to test count
               and branch back to loop body if non-zero. */
            tm.OnPc(0x800A0ED4u, [](const TraceContext& c) {
                LOG(Trace, "[HEX_LOOPCHK] @ED4 "
                           "R0(quotient)=0x%08X R5(remaining)=0x%08X\n",
                    c.regs[0], c.regs[5]);
            });

            /* Function exit at 0x800A0EDC. R4 = total digit count.
               Includes R9 — sub_800A0E74's prologue set R9 = base
               (16) for hex; the POP at 0x800A0EE0 must restore it
               to the caller's R9 value (the precision/width). */
            tm.OnPc(0x800A0EDCu, [](const TraceContext& c) {
                LOG(Trace, "[HEX_EXIT] @EDC "
                           "R0=0x%08X R4(total)=0x%08X "
                           "R9(base, pre-POP)=0x%08X SP=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[4], c.regs[9],
                    c.regs[13], c.regs[14]);
            });

            /* Just before the POP {R4-R10,LR} at 0x800A0EE0. SP
               points at the saved register block. */
            tm.OnPc(0x800A0EE0u, [](const TraceContext& c) {
                LOG(Trace, "[HEX_PRE_POP] @EE0 "
                           "R0=0x%08X R9=0x%08X SP=0x%08X "
                           "*[SP+20]=0x%08X (saved R9) *[SP+24]=0x%08X (saved R10)\n",
                    c.regs[0], c.regs[9], c.regs[13],
                    c.ReadVa32(c.regs[13] + 20).value_or(0xDEADBEEFu),
                    c.ReadVa32(c.regs[13] + 24).value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x800A143Cu, [](const TraceContext& c) {
                LOG(Trace, "[FMT_POST_HEX_CALL] @143C "
                           "R0(digits)=0x%08X R3=0x%08X R5=0x%08X "
                           "R6=0x%08X R9=0x%08X SP=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[3], c.regs[5],
                    c.regs[6], c.regs[9], c.regs[13], c.regs[14]);
            });

            /* The padding-count computation at 0x800A144C:
               SUB R3, R9, R0 — R3 ends up holding the precision
               difference. The next CMP R3 / BLE branches into the
               padding loop. */
            tm.OnPc(0x800A144Cu, [](const TraceContext& c) {
                LOG(Trace, "[FMT_PRE_SUB_R9_R0] @144C "
                           "R9=0x%08X R0=0x%08X (R3 will be R9-R0)\n",
                    c.regs[9], c.regs[0]);
            });

            tm.OnPc(0x800A1450u, [](const TraceContext& c) {
                LOG(Trace, "[FMT_AFTER_SUB] @1450 "
                           "R3(=R9-R0)=0x%08X R6=0x%08X (OLD R6)\n",
                    c.regs[3], c.regs[6]);
            });

            /* Just before LDR R2, [SP, #var_50] at 0x800A1448 —
               capture stack slot that becomes the field-width. */
            tm.OnPc(0x800A1448u, [](const TraceContext& c) {
                LOG(Trace, "[FMT_PRE_LDR_R2] @1448 "
                           "SP=0x%08X *[SP+0x14]=0x%08X (becomes R2=field_width)\n",
                    c.regs[13],
                    c.ReadVa32(c.regs[13] + 0x14u).value_or(0xDEADBEEFu));
            });

            /* After the SUB R6, R2, R0 at 0x800A1454 — R6 now
               holds the actual padding count (field_width - digits). */
            tm.OnPc(0x800A1458u, [](const TraceContext& c) {
                LOG(Trace, "[FMT_PADCOUNT] @1458 "
                           "R6(pad_count=R2-R0)=0x%08X R2=0x%08X R0=0x%08X\n",
                    c.regs[6], c.regs[2], c.regs[0]);
            });

            /* Entry of padding-loop CMP at 0x800A1504. R6 here is
               the actual loop counter; we should see it decrement
               down to 0. */
            tm.OnPc(0x800A1504u, [](const TraceContext& c) {
                LOG(Trace, "[FMT_PADLOOP_CHK] @1504 "
                           "R6(remaining)=0x%08X R3(char)=0x%08X R4(cursor)=0x%08X\n",
                    c.regs[6], c.regs[3], c.regs[4]);
            });

            constexpr uint32_t kHookPcs[] = {
                0x800A1458u, 0x800A145Cu, 0x800A1460u,
                0x800A1480u, 0x800A1484u, 0x800A1488u, 0x800A148Cu,
                0x800A1490u, 0x800A1494u, 0x800A1498u, 0x800A149Cu,
                0x800A14A0u, 0x800A14A4u, 0x800A14A8u, 0x800A14ACu,
                0x800A14B0u, 0x800A14B4u, 0x800A14B8u, 0x800A14BCu,
                0x800A14C0u, 0x800A14C4u, 0x800A14C8u, 0x800A14CCu,
                0x800A14D0u, 0x800A14D4u, 0x800A14D8u, 0x800A14DCu,
                0x800A14E0u, 0x800A14E4u, 0x800A14E8u,
            };
            for (uint32_t pc : kHookPcs) {
                tm.OnPc(pc, [pc](const TraceContext& c) {
                    LOG(Trace, "[STEP] pc=0x%08X "
                               "R3=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X\n",
                        pc, c.regs[3], c.regs[6], c.regs[7], c.regs[8]);
                });
            }

            tm.OnPc(0x80077B9Cu, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @B9C entry R0=0x%08X R1=0x%08X R2=0x%08X SP=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[13]);
            });
            tm.OnPc(0x80077BA0u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BA0 LDR  R1=0x%08X\n", c.regs[1]);
            });
            tm.OnPc(0x80077BA4u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BA4 AND  R2=0x%08X\n", c.regs[2]);
            });
            tm.OnPc(0x80077BA8u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BA8 CMP  R0=0x%08X\n", c.regs[0]);
            });
            tm.OnPc(0x80077BACu, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BAC BNE  R0=0x%08X (cmp w/0x10) CPSR=0x%08X\n",
                    c.regs[0], c.regs[15]);
            });
            tm.OnPc(0x80077BB0u, [](const TraceContext&) {
                LOG(Trace, "[BLK77B] @BB0 BNE-fallthrough\n");
            });
            tm.OnPc(0x80077BC8u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BC8 BNE-taken R1=0x%08X R2=0x%08X\n",
                    c.regs[1], c.regs[2]);
            });
            tm.OnPc(0x80077BD0u, [](const TraceContext&) {
                LOG(Trace, "[BLK77B] @BD0 BIC (Thumb-mode return path)\n");
            });
            tm.OnPc(0x80077BD4u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BD4 MSR CPSR_fc R2=0x%08X\n", c.regs[2]);
            });
            tm.OnPc(0x80077BF0u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BF0 LDM R1,{R1-LR} R1=0x%08X "
                           "[R1+0]=0x%08X [R1+0x34]=0x%08X (R14 slot)\n",
                    c.regs[1],
                    c.ReadVa32(c.regs[1]).value_or(0xDEADBEEFu),
                    c.ReadVa32(c.regs[1] + 0x34u).value_or(0xDEADBEEFu));
            });
            tm.OnPc(0x80077BF8u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @BF8 BX R0 R0=0x%08X (low-bit=Thumb)\n",
                    c.regs[0]);
            });
            tm.OnPc(0x80077C00u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @C00 BEQ-taken-target MSR CPSR R2=0x%08X\n",
                    c.regs[2]);
            });
            tm.OnPc(0x80077C04u, [](const TraceContext& c) {
                LOG(Trace, "[BLK77B] @C04 LDMIB R1,{R0-PC} R1=0x%08X "
                           "[R1+0x04]=0x%08X (R0) [R1+0x40]=0x%08X (PC slot)\n",
                    c.regs[1],
                    c.ReadVa32(c.regs[1] + 0x04u).value_or(0xDEADBEEFu),
                    c.ReadVa32(c.regs[1] + 0x40u).value_or(0xDEADBEEFu));
            });
            tm.OnPc(0x80077C08u, [](const TraceContext&) {
                LOG(Trace, "[BLK77B] @C08 STR R0,[R0+4] — past LDMIB\n");
            });

            tm.OnPc(0x8008AA58u, [](const TraceContext& c) {
                LOG(Trace, "[ABORT_TRIGGER] @AA58 R6=0x%08X [R6]=0x%08X "
                           "R4=0x%08X R7=0x%08X\n",
                    c.regs[6],
                    c.ReadVa32(c.regs[6]).value_or(0xDEADBEEFu),
                    c.regs[4], c.regs[7]);
            });
            /* Hooks inside the UND handler chain (per ANYPC trace,
               UND vector → 0x80077250 → 0x800775D4 → 0x800A2FA8 →
               sub_8007B8A4 → cascade). Find which step's emitted
               JIT code first produces broken state. */
            tm.OnPc(0x80077250u, [](const TraceContext& c) {
                LOG(Trace, "[UND_HANDLER] @77250 entry LR=0x%08X SP=0x%08X CPSR=0x%08X\n",
                    c.regs[14], c.regs[13], c.regs[15]);
            });
            tm.OnPc(0x800775D4u, [](const TraceContext& c) {
                LOG(Trace, "[UND_HANDLER] @775D4 main R0=0x%08X R1=0x%08X SP=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[13]);
            });
            tm.OnPc(0x800A2FA8u, [](const TraceContext& c) {
                LOG(Trace, "[UND_HANDLER] @A2FA8 R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3]);
            });
            tm.OnPc(0x8007B8A4u, [](const TraceContext& c) {
                LOG(Trace, "[UND_HANDLER] @7B8A4 (classifier) R0=0x%08X SP=0x%08X\n",
                    c.regs[0], c.regs[13]);
            });
            /* sub_80092364(v58[0], v19) — kernel's "can fault be
               resolved?" check. If returns 0, kernel classifies as
               STATUS_ACCESS_VIOLATION. Capture args at entry. */
            tm.OnPc(0x80092364u, [](const TraceContext& c) {
                LOG(Trace, "[FAULT_RESOLVE] @92364 entry R0(flag)=0x%08X R1(fault_addr)=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[14]);
            });

            tm.OnPc(0x8008AA30u, [](const TraceContext& c) {
                const uint32_t obj   = c.ReadVa32(c.regs[5]).value_or(0xDEADBEEFu);
                const uint32_t obj64 = c.ReadVa32(obj + 0x64u).value_or(0xDEADBEEFu);
                LOG(Trace, "[ABORT_BLK_AA30] R5=0x%08X [R5]=0x%08X "
                           "[R5]+0x64=0x%08X (val=0x%08X) R6=0x%08X [R6]=0x%08X SP=0x%08X\n",
                    c.regs[5], obj, obj + 0x64u, obj64,
                    c.regs[6],
                    c.ReadVa32(c.regs[6]).value_or(0xDEADBEEFu),
                    c.regs[13]);
            });
            /* Just before BL memset. R0=destination, R1=fill byte
               (=0), R2=count (=0x100). If R0 is invalid, memset's
               STM/STR aborts inside sub_800B35D8. */
            tm.OnPc(0x8008AA7Cu, [](const TraceContext& c) {
                LOG(Trace, "[MEMSET_CALL] @AA7C R0(dst)=0x%08X R1=0x%08X "
                           "R2=0x%08X R3=0x%08X [R3+0x24]=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.ReadVa32(c.regs[3] + 0x24u).value_or(0xDEADBEEFu));
            });
            /* If this fires, memset returned successfully — abort was
               NOT in memset. If it doesn't fire after MEMSET_CALL,
               memset aborted inside. */
            tm.OnPc(0x8008AA80u, [](const TraceContext& c) {
                LOG(Trace, "[MEMSET_RETURN] @AA80 R0=0x%08X\n", c.regs[0]);
            });

            /* Dump 16 bytes at 0x800BA894 (the address the kernel
               BX'd to) when the VTBL_CALL fires. Tells us whether
               this is real runtime kernel code (IDA's view says it's
               unmapped gap, but kernel branches there). */
            tm.OnPc(0x80096A40u, [](const TraceContext& c) {
                LOG(Trace, "[BYTES@0x800BA894] %08X %08X %08X %08X\n",
                    c.ReadVa32(0x800BA894u).value_or(0xDEADBEEFu),
                    c.ReadVa32(0x800BA898u).value_or(0xDEADBEEFu),
                    c.ReadVa32(0x800BA89Cu).value_or(0xDEADBEEFu),
                    c.ReadVa32(0x800BA8A0u).value_or(0xDEADBEEFu));
            });

            tm.OnPc(0x80096A3Cu, [](const TraceContext& c) {
                LOG(Trace, "[VTBL_CALL] @A3C "
                           "R0=0x%08X R3(target)=0x%08X R6(obj)=0x%08X "
                           "[R6+0x5C]=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[3], c.regs[6],
                    c.ReadVa32(c.regs[6] + 0x5Cu).value_or(0xDEADBEEFu),
                    c.regs[14]);
            });

            tm.OnRunLoopIter([](const TraceContext& c) {
                static uint32_t last_pc_slot = 0xDEADBEEFu;
                const uint32_t cur = c.ReadVa32(0xC201FB30u).value_or(0xDEADBEEFu);
                if (cur != last_pc_slot) {
                    LOG(Trace, "[CTX_PC_CHG] [0xC201FB30] = 0x%08X "
                               "(was 0x%08X) r15=0x%08X\n",
                        cur, last_pc_slot, c.regs[15]);
                    last_pc_slot = cur;
                }
            });

            tm.OnRunLoopIter([](const TraceContext& c) {
                static uint32_t last_val = 0xDEADBEEFu;
                const uint32_t cur = c.ReadVa32(0x814C621Cu).value_or(0xDEADBEEFu);
                if (cur != last_val) {
                    LOG(Trace, "[OBJ_SLOT_CHG] [0x814C621C] = 0x%08X "
                               "(was 0x%08X)\n", cur, last_val);
                    last_val = cur;
                }
            });

            tm.OnPc(0xFFFF000Cu, [](const TraceContext& c) {
                LOG(Trace, "[PABT_VEC] @FFFF000C "
                           "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                           "R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X "
                           "R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X "
                           "R12=0x%08X SP=0x%08X LR=0x%08X\n",
                    c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                    c.regs[4], c.regs[5], c.regs[6], c.regs[7],
                    c.regs[8], c.regs[9], c.regs[10], c.regs[11],
                    c.regs[12], c.regs[13], c.regs[14]);
            });
        });
    }
};

REGISTER_SERVICE(TraceWm5PrintfHexLoop);

}  /* namespace */
