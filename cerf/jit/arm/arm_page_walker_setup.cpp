#include "arm_page_walker.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../cpu/arm_processor_config.h"
#include "../../cpu/emulated_memory.h"
#include "arm_mmu.h"

REGISTER_SERVICE(ArmPageWalker);

bool ArmPageWalker::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmPageWalker::OnReady() {
    mmu_              = &emu_.Get<ArmMmu>();
    state_p_          = mmu_->State();
    memory_           = &emu_.Get<EmulatedMemory>();
    processor_config_ = &emu_.Get<ArmProcessorConfig>();
    static_aliases_    = emu_.Get<PageTableBuilder>().StaticRuntimeAliases();
    mmu_->BindWalker(this);
}

void ArmPageWalker::SetInjectionBand(uint32_t va_base, uint32_t pa_base,
                                     uint32_t size) {
    injection_band_va_   = va_base;
    injection_band_pa_   = pa_base;
    injection_band_size_ = size;
}

uint8_t* ArmPageWalker::ServeInjectionBand(uint32_t va, ArmMmuAccess access) {
    if (injection_band_size_ == 0u) return nullptr;
    const uint32_t off = va - injection_band_va_;
    if (off >= injection_band_size_) return nullptr;
    const uint32_t pa = injection_band_pa_ + off;
    last_pa_ = pa;
    const bool is_write = access == ArmMmuAccess::kWrite ||
                          access == ArmMmuAccess::kReadWrite;
    uint8_t* host = is_write ? memory_->TryTranslateWrite(pa)
                             : memory_->TryTranslate(pa);
    if (!host) return nullptr;
    if (access == ArmMmuAccess::kExecute) last_exec_pa_ = pa;
    return host;
}

void ArmPageWalker::LogBandFault(uint32_t va, uint32_t l1_word, bool have_l2,
                                 uint32_t l2_word) {
    if (injection_band_size_ == 0u ||
        va - injection_band_va_ >= injection_band_size_) {
        return;
    }
    const uint32_t ttbcr_n    = state_p_->ttbcr & 7u;
    const bool     use_ttbr1  = ttbcr_n != 0u &&
                                (va >> (32u - ttbcr_n)) != 0u;
    LOG(Mmu, "fault on injection-band VA 0x%08X: L1=0x%08X%s"
             " L2=0x%08X ttbr=%s ttbcr.n=%u asid=%02X\n",
        va, l1_word, have_l2 ? "" : " (no L2)", l2_word,
        use_ttbr1 ? "1" : "0", ttbcr_n,
        static_cast<uint8_t>(state_p_->contextidr & 0xFFu));
}
