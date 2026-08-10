#include "mips_insn_decoder.h"

#include <cstring>

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../cpu/mips_processor_config.h"
#include "mips_cpu.h"
#include "mips_cpu_state.h"
#include "mips_mmu.h"
#include "mips_place_fn_selector.h"
#include "mips_place_fns.h"

REGISTER_SERVICE(MipsInsnDecoder);

void MipsInsnDecoder::OnReady() {
    cpu_state_ = emu_.Get<MipsCpu>().State();
    mmu_       = &emu_.Get<MipsMmu>();
    memory_    = &emu_.Get<EmulatedMemory>();
    selector_  = &emu_.Get<MipsPlaceFnSelector>();

    auto& cfg = emu_.Get<MipsProcessorConfig>();
    const bool has_64bit = cfg.IsaLevel() != MipsIsaLevel::kMips1;
    const bool has_eret  = cfg.IsaLevel() != MipsIsaLevel::kMips1;
    const bool has_rfe   = cfg.IsaLevel() == MipsIsaLevel::kMips1;
    decoder_.Configure(cfg.HasFpu(), cfg.HasLlsc(),
                       cfg.IsaLevel() == MipsIsaLevel::kMips4,
                       cfg.HasVr41xxPowerModes(),
                       has_64bit, has_eret, has_rfe, cfg.HasMips16());
    m16_decoder_.Configure(has_64bit);
}

bool MipsInsnDecoder::Fetch16(MipsBlockContext* ctx, uint32_t va, uint16_t* hw,
                              uint32_t* pa) {
    const MipsTlbResult res =
        mmu_->Translate(cpu_state_, va, MipsAccess::kFetch, pa);
    if (res != MipsTlbResult::kMatch) {
        ctx->fetch_fault_pending = 1;
        ctx->fetch_fault_va      = va;
        ctx->fetch_fault_res     = res;
        return false;
    }
    uint8_t* host = memory_->TryTranslate(*pa);
    if (!host) {
        return false;
    }
    std::memcpy(hw, host, sizeof(*hw));
    return true;
}

void MipsInsnDecoder::Decode16(MipsBlockContext* ctx, uint32_t guest_pc) {
    guest_pc &= ~0x1u;
    const uint32_t block_start   = guest_pc;
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    const uint32_t page_end      = (guest_pc & ~page_off_mask) + page_off_mask + 1u;
    std::memset(ctx->insns, 0, sizeof(ctx->insns));

    uint32_t i = 0;
    bool     delay_pending = false;
    uint32_t jump_pc       = 0;
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i) {
        MipsDecodedInsn& insn = ctx->insns[i];

        uint16_t hw0 = 0, hw1 = 0;
        uint32_t pa0 = 0, pa1 = 0;
        if (!Fetch16(ctx, guest_pc, &hw0, &pa0)) {
            break;
        }
        const bool wide = Mips16Decoder::Needs4Bytes(hw0);
        if (wide) {
            const uint32_t va1 = guest_pc + 2u;
            if (!Fetch16(ctx, va1, &hw1, &pa1)) {
                break;
            }
            if ((va1 & ~page_off_mask) != (guest_pc & ~page_off_mask)) {
                ctx->tail_split   = va1 - block_start;
                ctx->tail_page_pa = pa1;
            }
        }

        /* U15509EJ2V0UM Table 3-12 p67: base PC = the insn's own PC, the
           EXTEND's PC, or - in a jump delay slot - the owning JR/JALR/JAL's PC. */
        const uint32_t base_pc = delay_pending ? jump_pc : guest_pc;

        uint32_t synth = 0;
        switch (m16_decoder_.Decode(hw0, hw1, guest_pc, base_pc, &insn, &synth)) {
            case Mips16DecodeKind::kSynth:
                if (!decoder_.Decode(synth, guest_pc, &insn)) {
                    LOG(Caution, "MipsInsnDecoder::Decode16:synthesized word 0x%08X "
                            "for halfword 0x%04X at 0x%08X is not decodable\n",
                        synth, hw0, guest_pc);
                    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
                }
                insn.place_fn = selector_->Select(&insn);
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
            ctx->insn0_pcrel_guard = 1;
        }

        /* U15509EJ2V0UM 3.8.3 p70: extended instructions and jump/branch
           instructions in a jump delay slot are unpredictable. */
        if (delay_pending && (wide || insn.is_branch || insn.ends_block)) {
            LOG(Caution, "MipsInsnDecoder::Decode16:illegal jump-delay-slot insn "
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

    ctx->num_insns = i;
}

void MipsInsnDecoder::Decode(MipsBlockContext* ctx, uint32_t guest_pc) {
    guest_pc &= ~0x3u;
    const uint32_t page_off_mask = (1u << cpu_state_->min_page_shift) - 1u;
    const uint32_t page_end = (guest_pc & ~page_off_mask) + page_off_mask + 1u;
    std::memset(ctx->insns, 0, sizeof(ctx->insns));

    uint32_t i = 0;
    bool delay_pending = false;
    for (; i < kMaxMipsInsnPerBlock && guest_pc < page_end; ++i, guest_pc += 4) {
        MipsDecodedInsn& insn = ctx->insns[i];

        uint32_t pa = 0;
        if (mmu_->Translate(cpu_state_, guest_pc, MipsAccess::kFetch, &pa) !=
            MipsTlbResult::kMatch) {
            break;
        }
        uint8_t* host = memory_->TryTranslate(pa);
        if (!host) {
            break;
        }

        uint32_t word;
        std::memcpy(&word, host, sizeof(word));
        if (decoder_.Decode(word, guest_pc, &insn)) {
            insn.place_fn = selector_->Select(&insn);
        } else {
            insn.place_fn = &PlaceMipsUndefined;
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
        }
    }

    /* A trailing branch (page end / cap before its delay slot) is KEPT, not
       dropped: its place fn set branch_state, the block exits with pc=branch+4,
       and the carried state resolves in the next block (QEMU DISAS_TOO_MANY +
       save_cpu_state). Dropping it was the cross-page "decoded 0 insns" defect. */
    ctx->num_insns = i;
}
