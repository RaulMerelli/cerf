#include "arm_mmu_probe.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "../../cpu/emulated_memory.h"
#include "arm_mmu.h"
#include "arm_pte.h"

REGISTER_SERVICE(ArmMmuProbe);

bool ArmMmuProbe::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmMmuProbe::OnReady() {
    state_p_          = emu_.Get<ArmMmu>().State();
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    static_aliases_    = emu_.Get<PageTableBuilder>().StaticRuntimeAliases();
}

std::optional<uint32_t> ArmMmuProbe::StaticAliasPa(uint32_t va) const {
    const uint32_t p = ArmFcseFold(va, state_p_->process_id);
    for (const auto& alias : static_aliases_) {
        if (p >= alias.va_base &&
            static_cast<uint64_t>(p) <
                static_cast<uint64_t>(alias.va_base) + alias.size) {
            return alias.pa_base + (p - alias.va_base);
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> ArmMmuProbe::WalkVaToPa(uint32_t va) {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.process_id);

    const uint32_t ttbcr_n    = state_.ttbcr & 7u;
    const uint32_t ttbr0_mask = ~((1u << (14u - ttbcr_n)) - 1u);
    const bool use_ttbr1 = ttbcr_n != 0u && (p >> (32u - ttbcr_n)) != 0u;
    const uint32_t l1_base = use_ttbr1
        ? (state_.ttbr1 & 0xFFFFC000u)
        : (state_.translation_table_base.word & ttbr0_mask);

    const uint32_t l1_pa = l1_base | ((p >> 20) << 2);
    uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return std::nullopt;
    ArmL1Pte l1_pte;
    l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);

    switch (l1_pte.fault.type) {
    case ArmL1PteType::kSection:
        return (l1_pte.section.section_base << 20) | (p & 0x000FFFFFu);

    case ArmL1PteType::kCoarse: {
        const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10)
                             | (((p >> 12) & 0xFFu) << 2);
        uint8_t* l2_host = memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) return std::nullopt;
        ArmL2Pte l2_pte;
        l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

        const bool v6_ext_small = processor_config_->HasCp15V6() &&
                                  !state_.effective_control_register.bits.xp;
        if (l2_pte.fault.type == ArmL2PteType::kSmallPage) {
            return (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
        }
        if (l2_pte.fault.type == ArmL2PteType::kExtendedSmallPage && v6_ext_small) {
            return ArmExtSmallPagePa(l2_pte.word, p);
        }
        return std::nullopt;
    }

    default:
        return std::nullopt;
    }
}

const ArmTlbEntry* ArmMmuProbe::MatchDataTlb(uint32_t va, uint32_t* folded) const {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.process_id);
    *folded = p;
    const uint8_t current_asid = static_cast<uint8_t>(state_.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(p);
    const int w = ArmTlbMatchWay(&state_.data_tlb, base, p & 0xFFFFF000u,
                                 current_asid, false);
    if (w < 0) return nullptr;
    return &state_.data_tlb.entries[base + static_cast<uint32_t>(w)];
}

std::optional<uint8_t*> ArmMmuProbe::PeekDataTlb(uint32_t va) const {
    uint32_t p = 0;
    const ArmTlbEntry* e = MatchDataTlb(va, &p);
    if (!e) return std::nullopt;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(p) + e->va_addend);
}

bool ArmMmuProbe::ExecPageGlobal(uint32_t folded_va) const {
    const ArmMmuState& state_ = *state_p_;
    const uint8_t current_asid = static_cast<uint8_t>(state_.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(folded_va);
    const int w = ArmTlbMatchWay(&state_.instruction_tlb, base,
                                 folded_va & 0xFFFFF000u, current_asid,
                                 /*need_write=*/false);
    return w >= 0 &&
           state_.instruction_tlb.entries[base + static_cast<uint32_t>(w)].global != 0u;
}

uint8_t* ArmMmuProbe::PeekVaToHost(uint32_t va) {
    const ArmMmuState& state_ = *state_p_;
    if (!state_.effective_control_register.bits.m) {
        const uint32_t pa = ArmFcseFold(va, state_.process_id);
        uint8_t* ram = memory_->TryTranslateWrite(pa);
        return ram ? ram : memory_->TryTranslate(pa);
    }

    if (const auto alias = StaticAliasPa(va)) {
        uint8_t* ram = memory_->TryTranslateWrite(*alias);
        return ram ? ram : memory_->TryTranslate(*alias);
    }
    if (std::optional<uint8_t*> tlb = PeekDataTlb(va)) return *tlb;

    std::optional<uint32_t> pa = WalkVaToPa(va);
    if (!pa) return nullptr;
    uint8_t* ram = memory_->TryTranslateWrite(*pa);
    return ram ? ram : memory_->TryTranslate(*pa);
}

bool ArmMmuProbe::PeekVaToPa(uint32_t va, uint32_t* pa) {
    const ArmMmuState& state_ = *state_p_;
    if (!state_.effective_control_register.bits.m) {
        *pa = ArmFcseFold(va, state_.process_id);
        return true;
    }

    if (const auto alias = StaticAliasPa(va)) {
        *pa = *alias;
        return true;
    }

    uint32_t p = 0;
    if (const ArmTlbEntry* e = MatchDataTlb(va, &p)) {
        *pa = e->pa_page | (p & 0x0FFFu);
        return true;
    }

    std::optional<uint32_t> walked = WalkVaToPa(va);
    if (!walked) return false;
    *pa = *walked;
    return true;
}
