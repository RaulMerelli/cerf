#include "../../jit/arm/coproc_emitter.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../jit/arm/cpu_state.h"
#include "../../jit/arm/arm_mmu_state.h"
#include "../../jit/arm/place_fns.h"
#include "../../jit/x86_emit.h"

#include <cstddef>

namespace {

constexpr uint32_t kRevidrStub = 0u;

uint8_t* EmitDiagnosticControl(uint8_t* cursor, DecodedInsn* d) {
    using namespace x86;
    const int32_t rd_disp = static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u);
    const int32_t value_disp = static_cast<int32_t>(offsetof(ArmMmuState, cortex_a9_diagnostic_control));
    if (d->l) {
        EmitMovRegBaseDisp32(cursor, kEax, kMmuReg, value_disp);
        EmitMovBaseDisp32Reg(cursor, kStateReg, rd_disp, kEax);
    } else {
        EmitMovRegBaseDisp32(cursor, kEax, kStateReg, rd_disp);
        EmitMovBaseDisp32Reg(cursor, kMmuReg, value_disp, kEax);
    }
    return cursor;
}

class CortexA9CoprocEmitter : public CoprocEmitter {
public:
    using CoprocEmitter::CoprocEmitter;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    uint8_t* EmitRegisterTransfer(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) override {
        if (d->cp_num == 15) {
            /* ARM DDI 0388I section 4.3.24; Linux imx6qdl.dtsi:intc. */
            if (d->l && d->cp_opc == 4 && d->crn == 15 && d->crm == 0 && d->cp == 0) {
                x86::EmitMovBaseDisp32Imm32(cursor, x86::kStateReg,
                                            static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u),
                                            0x00A00000u);
                return cursor;
            }
            if (d->l && d->cp_opc == 0 && d->crn == 0) {
                uint32_t value = 0;
                bool handled = true;
                if (d->crm == 0 && (d->cp == 2 || d->cp == 3)) {
                    value = 0u;
                } else if (d->crm == 0 && d->cp == 6) {
                    /* ARM DDI 0388I §4.3.4: REVIDR is implementation-specific and its reset
                       value is implementation-defined; CERF has no source for the i.MX6 value. */
                    value = kRevidrStub;
                } else if (d->crm == 0 && d->cp == 5) {
                    value = 0x80000000u;
                } else if (d->crm == 1) {
                    /* ARM DDI 0388I Table 4-16. */
                    static constexpr uint32_t kIdGroup1[8] = {
                        0x00001231u, 0x00000011u, 0x00010444u, 0x00000000u,
                        0x00100103u, 0x20000000u, 0x01230000u, 0x00102111u,
                    };
                    value = kIdGroup1[d->cp & 7u];
                } else if (d->crm == 2) {
                    /* ARM DDI 0388I Table 4-16. */
                    static constexpr uint32_t kIdIsar[8] = {
                        0x00101111u, 0x13112111u, 0x21232041u, 0x11112131u,
                        0x00011142u, 0x00000000u, 0x00000000u, 0x00000000u,
                    };
                    value = kIdIsar[d->cp & 7u];
                } else {
                    handled = false;
                }

                if (handled) {
                    x86::EmitMovBaseDisp32Imm32(cursor, x86::kStateReg,
                                                static_cast<int32_t>(offsetof(ArmCpuState, gprs) + d->rd * 4u), value);
                    return cursor;
                }
            }
            /* ARM UAN 0005D erratum 845369: the Cortex-A9 Diagnostic Control
               Register is encoded as p15,0,Rt,c15,c0,1. */
            if (d->cp_opc == 0 && d->crn == 15 && d->crm == 0 && d->cp == 1) {
                return EmitDiagnosticControl(cursor, d);
            }
            return EmitCp15RegisterTransfer(cursor, d, ctx);
        }
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpRegisterTransfer(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataTransfer(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpDataTransfer(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }

    uint8_t* EmitDataOperation(uint8_t* cursor, DecodedInsn* d, BlockContext* ctx) override {
        if (d->cp_num == 10 || d->cp_num == 11) {
            return EmitVfpDataOperation(cursor, d, ctx);
        }
        return EmitRaiseUndAndReturn(cursor, d, ctx);
    }
};

}

REGISTER_SERVICE_AS(CortexA9CoprocEmitter, CoprocEmitter);
