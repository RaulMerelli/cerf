#include "mips_jit.h"

#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "mips_place_fns.h"

bool MipsJit::Fetch16(uint32_t va, uint16_t* hw, uint32_t* pa) {
    const MipsTlbResult res =
        mmu_->Translate(&cpu_state_, va, MipsAccess::kFetch, pa);
    if (res != MipsTlbResult::kMatch) {
        block_ctx_.fetch_fault_pending = 1;
        block_ctx_.fetch_fault_va      = va;
        block_ctx_.fetch_fault_res     = res;
        return false;
    }
    uint8_t* host = memory_->TryTranslate(*pa);
    if (!host) {
        return false;
    }
    std::memcpy(hw, host, sizeof(*hw));
    return true;
}

void MipsJit::JitDecode16(uint32_t guest_pc) {
    guest_pc &= ~0x1u;
    const uint32_t block_start   = guest_pc;
    const uint32_t page_off_mask = (1u << cpu_state_.min_page_shift) - 1u;
    const uint32_t page_end      = (guest_pc & ~page_off_mask) + page_off_mask + 1u;
    std::memset(block_ctx_.insns, 0, sizeof(block_ctx_.insns));

    uint32_t i = 0;
    bool     delay_pending = false;
    uint32_t jump_pc       = 0;
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i) {
        MipsDecodedInsn& insn = block_ctx_.insns[i];

        uint16_t hw0 = 0, hw1 = 0;
        uint32_t pa0 = 0, pa1 = 0;
        if (!Fetch16(guest_pc, &hw0, &pa0)) {
            break;
        }
        const bool wide = Mips16Decoder::Needs4Bytes(hw0);
        if (wide) {
            const uint32_t va1 = guest_pc + 2u;
            if (!Fetch16(va1, &hw1, &pa1)) {
                break;
            }
            if ((va1 & ~page_off_mask) != (guest_pc & ~page_off_mask)) {
                block_ctx_.tail_split   = va1 - block_start;
                block_ctx_.tail_page_pa = pa1;
            }
        }

        /* U15509EJ2V0UM Table 3-12 p67: base PC = the insn's own PC, the
           EXTEND's PC, or - in a jump delay slot - the owning JR/JALR/JAL's PC. */
        const uint32_t base_pc = delay_pending ? jump_pc : guest_pc;

        uint32_t synth = 0;
        switch (m16_decoder_.Decode(hw0, hw1, guest_pc, base_pc, &insn, &synth)) {
            case Mips16DecodeKind::kSynth:
                if (!decoder_.Decode(synth, guest_pc, &insn)) {
                    LOG(Caution, "MipsJit::JitDecode16: synthesized word 0x%08X "
                            "for halfword 0x%04X at 0x%08X is not decodable\n",
                        synth, hw0, guest_pc);
                    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
                }
                insn.place_fn = SelectPlaceFn(&insn);
                break;
            case Mips16DecodeKind::kDirect:
                insn.guest_address = guest_pc;
                break;
            case Mips16DecodeKind::kReserved:
                insn.guest_address = guest_pc;
                insn.place_fn      = &PlaceMipsUndefined;
                break;
        }
        insn.length = wide ? 4u : 2u;
        insn.raw    = (static_cast<uint32_t>(hw1) << 16) | hw0;
        if (i == 0 && (insn.place_fn == &PlaceMips16Addiupc ||
                       insn.place_fn == &PlaceMips16Lwpc ||
                       insn.place_fn == &PlaceMips16Ldpc)) {
            block_ctx_.insn0_pcrel_guard = 1;
        }

        /* U15509EJ2V0UM 3.8.3 p70: extended instructions and jump/branch
           instructions in a jump delay slot are unpredictable. */
        if (delay_pending && (wide || insn.is_branch || insn.ends_block)) {
            LOG(Caution, "MipsJit::JitDecode16: illegal jump-delay-slot insn "
                    "0x%08X at 0x%08X (wide=%d branch=%d ends=%d)\n",
                insn.raw, guest_pc, wide ? 1 : 0, insn.is_branch, insn.ends_block);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }

        if (delay_pending) {
            ++i;
            break;
        }
        if (insn.ends_block) {
            ++i;
            break;
        }
        if (insn.is_branch) {
            delay_pending = true;
            jump_pc       = guest_pc;
        }
        guest_pc += insn.length;
    }

    block_ctx_.num_insns = i;
}
