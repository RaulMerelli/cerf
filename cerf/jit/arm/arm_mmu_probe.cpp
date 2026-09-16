#include "arm_mmu_probe.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "../../cpu/emulated_memory.h"
#include "../../cpu/physical_address_mapper.h"
#include "arm_mmu.h"
#include "arm_page_walker.h"
#include "arm_par_attributes.h"
#include "arm_pte.h"

REGISTER_SERVICE(ArmMmuProbe);

bool ArmMmuProbe::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmMmuProbe::OnReady() {
    state_p_          = emu_.Get<ArmMmu>().State();
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    address_mapper_   = &emu_.Get<PhysicalAddressMapper>();
}

bool ArmMmuProbe::InjectionBandPa(uint32_t va, uint32_t* pa) const {
    const ArmMmuState& state_ = *state_p_;
    if (!state_.effective_control_register.bits.m) return false;
    const uint32_t p = ArmFcseFold(va, state_.fcse_fold_id);
    const uint32_t l1_pa = ArmL1DescriptorAddress(
        p, state_.ttbcr, state_.translation_table_base.word, state_.ttbr1);
    uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return false;
    ArmL1Pte l1_pte;
    l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);
    if (l1_pte.fault.type == ArmL1PteType::kFault)
        return emu_.Get<ArmPageWalker>().InjectionBandPa(p, pa);
    return false;
}

std::optional<uint32_t> ArmMmuProbe::WalkVaToPa(uint32_t va) {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.fcse_fold_id);

    const uint32_t l1_pa = ArmL1DescriptorAddress(
        p, state_.ttbcr, state_.translation_table_base.word, state_.ttbr1);
    uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return std::nullopt;
    ArmL1Pte l1_pte;
    l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);

    switch (l1_pte.fault.type) {
    case ArmL1PteType::kSection: {
        const ArmSupersectionFormat format = ArmEffectiveSupersectionFormat(
            processor_config_->SupersectionFormat(), state_.effective_control_register.bits.xp);
        const ArmSectionTranslation translation =
            ArmTranslateSection(l1_pte.word, p, format);
        uint32_t system_pa = 0;
        if (!address_mapper_->Map(translation.physical_address, 1u, system_pa))
            return std::nullopt;
        return system_pa;
    }

    case ArmL1PteType::kCoarse: {
        const uint32_t l2_pa = (l1_pte.coarse.page_table_base << 10)
                             | (((p >> 12) & 0xFFu) << 2);
        uint8_t* l2_host = memory_->TryTranslateWrite(l2_pa);
        if (!l2_host) return std::nullopt;
        ArmL2Pte l2_pte;
        l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

        const bool v6_ext_small = processor_config_->HasCp15V6() &&
                                  !state_.effective_control_register.bits.xp;
        uint32_t cpu_pa = 0;
        if (l2_pte.fault.type == ArmL2PteType::kSmallPage) {
            cpu_pa = (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
        } else if (l2_pte.fault.type == ArmL2PteType::kExtendedSmallPage && v6_ext_small) {
            cpu_pa = ArmExtSmallPagePa(l2_pte.word, p);
        } else {
            return std::nullopt;
        }
        uint32_t system_pa = 0;
        if (!address_mapper_->Map(cpu_pa, 1u, system_pa)) return std::nullopt;
        return system_pa;
    }

    default:
        return std::nullopt;
    }
}

const ArmTlbEntry* ArmMmuProbe::MatchDataTlb(uint32_t va, uint32_t* folded) const {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.fcse_fold_id);
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
    return w >= 0 && ArmTlbGlobal(
           state_.instruction_tlb.entries[base + static_cast<uint32_t>(w)]);
}

uint8_t* ArmMmuProbe::PeekVaToHost(uint32_t va) {
    const ArmMmuState& state_ = *state_p_;
    if (!state_.effective_control_register.bits.m) {
        const uint32_t pa = ArmFcseFold(va, state_.fcse_fold_id);
        uint8_t* ram = memory_->TryTranslateWrite(pa);
        return ram ? ram : memory_->TryTranslate(pa);
    }

    if (std::optional<uint8_t*> tlb = PeekDataTlb(va)) return *tlb;

    uint32_t band_pa = 0;
    if (InjectionBandPa(va, &band_pa)) {
        uint8_t* ram = memory_->TryTranslateWrite(band_pa);
        return ram ? ram : memory_->TryTranslate(band_pa);
    }

    std::optional<uint32_t> pa = WalkVaToPa(va);
    if (!pa) return nullptr;
    uint8_t* ram = memory_->TryTranslateWrite(*pa);
    return ram ? ram : memory_->TryTranslate(*pa);
}

bool ArmMmuProbe::PeekVaToPa(uint32_t va, uint32_t* pa) {
    const ArmMmuState& state_ = *state_p_;
    if (!state_.effective_control_register.bits.m) {
        *pa = ArmFcseFold(va, state_.fcse_fold_id);
        return true;
    }

    uint32_t p = 0;
    if (const ArmTlbEntry* e = MatchDataTlb(va, &p)) {
        *pa = e->pa_page | (p & 0x0FFFu);
        return true;
    }

    if (InjectionBandPa(va, pa)) return true;

    std::optional<uint32_t> walked = WalkVaToPa(va);
    if (!walked) return false;
    *pa = *walked;
    return true;
}

bool ArmMmuProbe::TlbPar(uint32_t va, uint32_t* pa, uint16_t* attrs) const {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.fcse_fold_id);
    if (!state_.effective_control_register.bits.m) {
        *pa = p;
        *attrs = ArmMmuDisabledDataParAttributes();
        return true;
    }
    const uint8_t asid = static_cast<uint8_t>(state_.contextidr & 0xFFu);
    const uint32_t base = ArmTlbSetBase(p);
    const uint32_t page = p & 0xFFFFF000u;
    int w = ArmTlbMatchWay(&state_.data_tlb, base, page, asid, false);
    if (w < 0) w = ArmTlbMatchIoWay(&state_.data_tlb, base, page, asid, false);
    if (w >= 0) {
        const ArmTlbEntry& e = state_.data_tlb.entries[base + static_cast<uint32_t>(w)];
        *pa = e.pa_page | (p & 0x0FFFu);
        *attrs = ArmTlbParAttributes(e);
        return true;
    }
    if (!InjectionBandPa(va, pa)) return false;
    /* ARM DDI 0406C.d B4.1.112: zero PAR attributes encode Normal,
       non-cacheable, non-shareable memory for this CERF-owned band. */
    *attrs = 0u;
    return true;
}

bool ArmMmuProbe::WalkPar(uint32_t va, uint32_t* pa, uint16_t* attrs) const {
    const ArmMmuState& state_ = *state_p_;
    const uint32_t p = ArmFcseFold(va, state_.fcse_fold_id);
    const uint32_t l1_pa = ArmL1DescriptorAddress(
        p, state_.ttbcr, state_.translation_table_base.word, state_.ttbr1);
    uint8_t* l1_host = memory_->TryTranslateWrite(l1_pa);
    if (!l1_host) return false;
    ArmL1Pte l1_pte;
    l1_pte.word = *reinterpret_cast<uint32_t*>(l1_host);
    const bool modern = processor_config_->HasCp15V6() && state_.effective_control_register.bits.xp;

    if (l1_pte.fault.type == ArmL1PteType::kFault) {
        if (!InjectionBandPa(va, pa)) return false;
        *attrs = 0u;
        return true;
    }

    uint64_t cpu_pa = 0;
    if (l1_pte.fault.type == ArmL1PteType::kSection) {
        const ArmSupersectionFormat format = ArmEffectiveSupersectionFormat(
            processor_config_->SupersectionFormat(), state_.effective_control_register.bits.xp);
        cpu_pa = ArmTranslateSection(l1_pte.word, p, format).physical_address;
        *attrs = ArmSectionParAttributes(state_, l1_pte.word, modern);
    } else if (l1_pte.fault.type == ArmL1PteType::kCoarse) {
        uint8_t* l2_host = memory_->TryTranslateWrite((l1_pte.coarse.page_table_base << 10) | (((p >> 12) & 0xFFu) << 2));
        if (!l2_host) return false;
        ArmL2Pte l2_pte;
        l2_pte.word = *reinterpret_cast<uint32_t*>(l2_host);

        switch (l2_pte.fault.type) {
        case ArmL2PteType::kSmallPage:
            cpu_pa = (l2_pte.small_page.small_page_base << 12) | (p & 0x0FFFu);
            *attrs = ArmSmallPageParAttributes(state_, l2_pte.word, l1_pte.word, modern);
            break;
        case ArmL2PteType::kLargePage:
            cpu_pa = (l2_pte.large_page.large_page_base << 16) | (p & 0x0000FFFFu);
            *attrs = ArmLargePageParAttributes(state_, l2_pte.word, l1_pte.word, modern);
            break;
        default: return false;
        }
    } else {
        return false;
    }
    return address_mapper_->Map(cpu_pa, 1u, *pa);
}
