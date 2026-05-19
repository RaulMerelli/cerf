#include "arm_vfp.h"

#include <cmath>
#include <cstring>

#include "../core/cerf_emulator.h"
#include "arm_cpu.h"
#include "arm_mmu.h"

REGISTER_SERVICE(ArmVfp);

uint32_t ArmVfp::HandleBlockTransfer(uint32_t pc, uint32_t rn_idx, uint32_t vd,
                                     uint32_t imm8, uint32_t flags) {
    auto& cpu = emu_.Get<ArmCpu>();
    auto& mmu = emu_.Get<ArmMmu>();
    auto* state = cpu.State();

    const bool is_load       = (flags & kFlagL)  != 0;
    const bool writeback     = (flags & kFlagW)  != 0;
    const bool pre_decrement = (flags & kFlagP)  != 0;
    const bool is_dp         = (flags & kFlagDp) != 0;

    const uint32_t n_regs    = is_dp ? (imm8 >> 1) : imm8;
    const uint32_t bytes_per = is_dp ? 8u : 4u;

    /* UNPREDICTABLE encodings — QEMU chooses to UND. */
    if (n_regs == 0 || (vd + n_regs) > 32u ||
        (is_dp && n_regs > 16u) || rn_idx == 15u) {
        cpu.RaiseUndefinedException(pc);
        return 1;
    }

    uint32_t addr = state->gprs[rn_idx];
    if (pre_decrement) {
        addr -= imm8 * 4u;
    }

    uint8_t* vfp_base = reinterpret_cast<uint8_t*>(state->vfp_d);

    for (uint32_t i = 0; i < n_regs; i++) {
        int8_t   tlb_hint = 0;
        uint8_t* host = is_load
            ? mmu.TranslateRead (state, addr, &tlb_hint)
            : mmu.TranslateWrite(state, addr, &tlb_hint);
        if (!host) {
            cpu.RaiseAbortDataException(pc);
            return 1;
        }
        const uint32_t off = is_dp ? ((vd + i) * 8u) : ((vd + i) * 4u);
        if (is_load) {
            std::memcpy(vfp_base + off, host, bytes_per);
        } else {
            std::memcpy(host, vfp_base + off, bytes_per);
        }
        addr += bytes_per;
    }

    if (writeback) {
        if (pre_decrement) {
            addr -= n_regs * bytes_per;
        } else if (is_dp && (imm8 & 1u)) {
            /* Odd imm8 DP encoding leaves an extra word of stride. */
            addr += 4u;
        }
        state->gprs[rn_idx] = addr;
    }
    return 0;
}

uint32_t __cdecl ArmVfp::HandleBlockTransferHelper(ArmVfp*  vfp,
                                                   uint32_t pc,
                                                   uint32_t rn_idx,
                                                   uint32_t vd,
                                                   uint32_t imm8,
                                                   uint32_t flags) {
    return vfp->HandleBlockTransfer(pc, rn_idx, vd, imm8, flags);
}

uint32_t ArmVfp::HandleSingleTransfer(uint32_t pc, uint32_t rn_idx, uint32_t vd,
                                      int32_t signed_off, uint32_t flags) {
    auto& cpu = emu_.Get<ArmCpu>();
    auto& mmu = emu_.Get<ArmMmu>();
    auto* state = cpu.State();

    const bool is_load = (flags & kFlagL)  != 0;
    const bool is_dp   = (flags & kFlagDp) != 0;
    const uint32_t bytes = is_dp ? 8u : 4u;

    /* Rn=15 (PC) is the PC-relative form: address base is the
       current insn's PC + 8 (ARM pipeline view), word-aligned. */
    uint32_t addr;
    if (rn_idx == 15u) {
        addr = (pc + 8u) & ~3u;
    } else {
        addr = state->gprs[rn_idx];
    }
    addr += static_cast<uint32_t>(signed_off);

    int8_t   tlb_hint = 0;
    uint8_t* host = is_load
        ? mmu.TranslateRead (state, addr, &tlb_hint)
        : mmu.TranslateWrite(state, addr, &tlb_hint);
    if (!host) {
        cpu.RaiseAbortDataException(pc);
        return 1;
    }

    uint8_t* vfp_base = reinterpret_cast<uint8_t*>(state->vfp_d);
    const uint32_t off = is_dp ? (vd * 8u) : (vd * 4u);
    if (is_load) {
        std::memcpy(vfp_base + off, host, bytes);
    } else {
        std::memcpy(host, vfp_base + off, bytes);
    }
    return 0;
}

uint32_t __cdecl ArmVfp::HandleSingleTransferHelper(ArmVfp*  vfp,
                                                   uint32_t pc,
                                                   uint32_t rn_idx,
                                                   uint32_t vd,
                                                   int32_t  signed_off,
                                                   uint32_t flags) {
    return vfp->HandleSingleTransfer(pc, rn_idx, vd, signed_off, flags);
}

namespace {

inline uint32_t VfpCmpNzcv(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) return 0x3u;
    if (a < b) return 0x8u;
    if (a > b) return 0x2u;
    return 0x6u;
}

inline void StoreFpscrNzcv(ArmCpuState* state, uint32_t nzcv4) {
    state->fpscr = (state->fpscr & ~0xF0000000u) | (nzcv4 << 28);
}

}  /* namespace */

uint32_t ArmVfp::ExecuteCdp(uint32_t pc, uint32_t packed) {
    auto& cpu = emu_.Get<ArmCpu>();
    auto* state = cpu.State();

    const uint32_t crm    =  packed        & 0xFu;
    const uint32_t cp_bits= (packed >> 4)  & 0x7u;
    const uint32_t cp_num = (packed >> 7)  & 0xFu;
    const uint32_t crd    = (packed >> 11) & 0xFu;
    const uint32_t crn    = (packed >> 15) & 0xFu;
    const uint32_t cp_opc = (packed >> 19) & 0xFu;

    if (cp_num != 10u && cp_num != 11u) {
        cpu.RaiseUndefinedException(pc);
        return 1;
    }
    const bool is_dp = (cp_num == 11u);

    const uint32_t T   = (cp_opc >> 3) & 1u;
    const uint32_t D   = (cp_opc >> 2) & 1u;
    const uint32_t opc =  cp_opc       & 3u;
    const uint32_t N   = (cp_bits >> 2) & 1u;
    const uint32_t op6 = (cp_bits >> 1) & 1u;
    const uint32_t M   =  cp_bits       & 1u;

    uint32_t sd, sn, sm;
    if (is_dp) {
        sd = (D << 4) | crd;
        sn = (N << 4) | crn;
        sm = (M << 4) | crm;
    } else {
        sd = (crd << 1) | D;
        sn = (crn << 1) | N;
        sm = (crm << 1) | M;
    }

    float*  sp_regs = reinterpret_cast<float*> (state->vfp_d);
    double* dp_regs = reinterpret_cast<double*>(state->vfp_d);

    if (T == 0u) {
        const uint32_t key = (opc << 1) | op6;
        if (is_dp) {
            const double dn = dp_regs[sn];
            const double dm = dp_regs[sm];
            const double dd = dp_regs[sd];
            double r = 0.0;
            switch (key) {
                case 0: r =  dd + dn * dm;  break;  /* VMLA  */
                case 1: r =  dd - dn * dm;  break;  /* VMLS  */
                case 2: r = -dd + dn * dm;  break;  /* VNMLS */
                case 3: r = -dd - dn * dm;  break;  /* VNMLA */
                case 4: r =  dn * dm;       break;  /* VMUL  */
                case 5: r = -(dn * dm);     break;  /* VNMUL */
                case 6: r =  dn + dm;       break;  /* VADD  */
                case 7: r =  dn - dm;       break;  /* VSUB  */
            }
            dp_regs[sd] = r;
        } else {
            const float fn = sp_regs[sn];
            const float fm = sp_regs[sm];
            const float fd = sp_regs[sd];
            float r = 0.0f;
            switch (key) {
                case 0: r =  fd + fn * fm;  break;
                case 1: r =  fd - fn * fm;  break;
                case 2: r = -fd + fn * fm;  break;
                case 3: r = -fd - fn * fm;  break;
                case 4: r =  fn * fm;       break;
                case 5: r = -(fn * fm);     break;
                case 6: r =  fn + fm;       break;
                case 7: r =  fn - fm;       break;
            }
            sp_regs[sd] = r;
        }
        return 0;
    }

    /* T = 1 */
    if (opc == 0u && op6 == 0u) {
        /* VDIV */
        if (is_dp) dp_regs[sd] = dp_regs[sn] / dp_regs[sm];
        else       sp_regs[sd] = sp_regs[sn] / sp_regs[sm];
        return 0;
    }

    if (opc != 3u) {
        cpu.RaiseUndefinedException(pc);
        return 1;
    }

    const uint32_t bit7 = (cp_bits >> 2) & 1u;
    const uint32_t bit6 =  op6;
    const uint32_t op_sel = (bit7 << 1) | bit6;

    switch (crn) {
        case 0x0: {
            if (op_sel == 0u) {
                const uint32_t imm4h = crn;
                const uint32_t imm4l = crm;
                const uint32_t imm8 = (imm4h << 4) | imm4l;
                const uint32_t a = (imm8 >> 7) & 1u;
                const uint32_t b = (imm8 >> 6) & 1u;
                const uint32_t cdef = imm8 & 0x3Fu;
                if (is_dp) {
                    uint64_t bits = (static_cast<uint64_t>(a) << 63)
                                  | (static_cast<uint64_t>(!b ? 1u : 0u) << 62)
                                  | (static_cast<uint64_t>(b ? 0xFFu : 0u) << 54)
                                  | (static_cast<uint64_t>(cdef) << 48);
                    std::memcpy(&dp_regs[sd], &bits, 8);
                } else {
                    uint32_t bits = (a << 31)
                                  | ((!b ? 1u : 0u) << 30)
                                  | ((b ? 0x1Fu : 0u) << 25)
                                  | (cdef << 19);
                    std::memcpy(&sp_regs[sd], &bits, 4);
                }
                return 0;
            }
            if (op_sel == 1u) {
                /* VMOV (register) — Vd = Vm */
                if (is_dp) dp_regs[sd] = dp_regs[sm];
                else       sp_regs[sd] = sp_regs[sm];
                return 0;
            }
            if (op_sel == 3u) {
                /* VABS */
                if (is_dp) dp_regs[sd] = std::fabs(dp_regs[sm]);
                else       sp_regs[sd] = std::fabs(sp_regs[sm]);
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x1: {
            if (op_sel == 1u) {
                /* VNEG */
                if (is_dp) dp_regs[sd] = -dp_regs[sm];
                else       sp_regs[sd] = -sp_regs[sm];
                return 0;
            }
            if (op_sel == 3u) {
                /* VSQRT */
                if (is_dp) dp_regs[sd] = std::sqrt(dp_regs[sm]);
                else       sp_regs[sd] = std::sqrt(sp_regs[sm]);
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x4: {
            /* VCMP / VCMPE — quiet vs signalling, same NZCV pack. */
            const double a = is_dp ? dp_regs[sd] : static_cast<double>(sp_regs[sd]);
            const double b = is_dp ? dp_regs[sm] : static_cast<double>(sp_regs[sm]);
            StoreFpscrNzcv(state, VfpCmpNzcv(a, b));
            return 0;
        }
        case 0x5: {
            /* VCMP0 / VCMPE0 — compare against +0.0. */
            const double a = is_dp ? dp_regs[sd] : static_cast<double>(sp_regs[sd]);
            StoreFpscrNzcv(state, VfpCmpNzcv(a, 0.0));
            return 0;
        }
        case 0x7: {
            /* VCVT (precision conversion) at op_sel == 3. */
            if (op_sel == 3u) {
                if (is_dp) {
                    /* cp==11 means dest is DP — VCVT.F64.F32 Dd, Sm */
                    const float src = sp_regs[(crm << 1) | M];
                    dp_regs[sd] = static_cast<double>(src);
                } else {
                    /* cp==10 means dest is SP — VCVT.F32.F64 Sd, Dm */
                    const double src = dp_regs[(M << 4) | crm];
                    sp_regs[sd] = static_cast<float>(src);
                }
                return 0;
            }
            cpu.RaiseUndefinedException(pc);
            return 1;
        }
        case 0x8: {
            /* VCVT int → FP: source is integer in Sm (always 32-bit
               int, source register is SP-form regardless of cp).
               bit[7]=signed-flag (0=u32, 1=s32). */
            const uint32_t src_sp_idx = (crm << 1) | M;
            uint32_t src_int;
            std::memcpy(&src_int, &sp_regs[src_sp_idx], 4);
            const bool is_signed = (bit7 != 0u);
            if (is_dp) {
                dp_regs[sd] = is_signed
                    ? static_cast<double>(static_cast<int32_t>(src_int))
                    : static_cast<double>(src_int);
            } else {
                sp_regs[sd] = is_signed
                    ? static_cast<float>(static_cast<int32_t>(src_int))
                    : static_cast<float>(src_int);
            }
            return 0;
        }
        case 0xC:
        case 0xD: {
            const bool is_signed = (crn == 0xDu);
            uint32_t result;
            if (is_dp) {
                const double v = dp_regs[sm];
                result = is_signed
                    ? static_cast<uint32_t>(static_cast<int32_t>(v))
                    : static_cast<uint32_t>(v);
            } else {
                const float v = sp_regs[sm];
                result = is_signed
                    ? static_cast<uint32_t>(static_cast<int32_t>(v))
                    : static_cast<uint32_t>(v);
            }
            /* Dest is always SP-form (32-bit int). */
            const uint32_t dst_sp_idx = (crd << 1) | D;
            std::memcpy(&sp_regs[dst_sp_idx], &result, 4);
            return 0;
        }
        default:
            cpu.RaiseUndefinedException(pc);
            return 1;
    }
}

uint32_t __cdecl ArmVfp::ExecuteCdpHelper(ArmVfp*  vfp,
                                          uint32_t pc,
                                          uint32_t packed) {
    return vfp->ExecuteCdp(pc, packed);
}
