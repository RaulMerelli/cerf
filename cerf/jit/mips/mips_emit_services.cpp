#include "mips_emit_services.h"

#include "../../core/cerf_emulator.h"
#include "../../cpu/mips_processor_config.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "mips_cp0_emitter.h"
#include "mips_cp0_ops.h"
#include "mips_cpu.h"
#include "mips_exception_delivery.h"
#include "mips_insn_decoder.h"
#include "mips_interrupt_channel.h"
#include "mips_memory_access.h"
#include "mips_mmu.h"
#include "mips_translation_cache.h"
#include "mips_wide_arithmetic.h"

REGISTER_SERVICE(MipsEmitServices);

void MipsEmitServices::OnReady() {
    cpu_         = &emu_.Get<MipsCpu>();
    cpu_state_   = cpu_->State();
    mmu_         = &emu_.Get<MipsMmu>();
    cpu_config_  = &emu_.Get<MipsProcessorConfig>();
    cp0_emitter_ = &emu_.Get<MipsCp0Emitter>();
    peripheral_  = &emu_.Get<PeripheralDispatcher>();

    cache_         = &emu_.Get<MipsTranslationCache>();
    memory_access_ = &emu_.Get<MipsMemoryAccess>();
    wide_          = &emu_.Get<MipsWideArithmetic>();
    cp0_ops_       = &emu_.Get<MipsCp0Ops>();
    exceptions_    = &emu_.Get<MipsExceptionDelivery>();
    channel_       = &emu_.Get<MipsInterruptChannel>();
}
