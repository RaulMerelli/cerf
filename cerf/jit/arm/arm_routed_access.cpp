#include "arm_routed_access.h"

#include <cstring>

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "arm_mmu.h"
#include "arm_page_walker.h"

REGISTER_SERVICE(ArmRoutedAccess);

bool ArmRoutedAccess::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Arm;
}

void ArmRoutedAccess::OnReady() {
    mmu_        = &emu_.Get<ArmMmu>();
    walker_     = &emu_.Get<ArmPageWalker>();
    dispatcher_ = &emu_.Get<PeripheralDispatcher>();
}

void ArmRoutedAccess::HaltUnalignedRouted(uint32_t guest_pc, uint32_t va,
                                          uint32_t bytes, uint32_t pa,
                                          const char* kind) {
    LOG(Caution, "ArmRoutedAccess: guest PC 0x%08X routes an unaligned %u-byte "
            "%s at VA 0x%08X to peripheral PA 0x%08X\n",
        guest_pc, bytes, kind, va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void ArmRoutedAccess::HaltRoutedWidth(uint32_t guest_pc, uint32_t va,
                                      uint32_t bytes, uint32_t pa,
                                      const char* kind) {
    LOG(Caution, "ArmRoutedAccess: guest PC 0x%08X routes a %u-byte %s at "
            "VA 0x%08X to peripheral PA 0x%08X with no modeled width\n",
        guest_pc, bytes, kind, va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

bool ArmRoutedAccess::Load(ArmCpuState* cpu_state, uint32_t guest_pc,
                           uint32_t va, uint32_t bytes, uint32_t* out) {
    if ((va & (bytes - 1u)) != 0u) {
        uint32_t value = 0;
        if (mmu_->AccessPaged(cpu_state, va,
                              reinterpret_cast<uint8_t*>(&value), bytes, true)) {
            *out = value;
            return true;
        }
        const uint32_t split_pa = mmu_->io_pending_address();
        if (split_pa == 0u) {
            return false;
        }
        HaltUnalignedRouted(guest_pc, va, bytes, split_pa, "load");
    }

    uint8_t* host = walker_->TranslateRead(cpu_state, va);
    if (host != nullptr) {
        uint32_t value = 0;
        std::memcpy(&value, host, bytes);
        *out = value;
        return true;
    }
    const uint32_t pa = mmu_->io_pending_address();
    if (pa == 0u) {
        return false;
    }
    switch (bytes) {
    case 1u: *out = dispatcher_->ReadByte(pa); return true;
    case 2u: *out = dispatcher_->ReadHalf(pa); return true;
    case 4u: *out = dispatcher_->ReadWord(pa); return true;
    default: HaltRoutedWidth(guest_pc, va, bytes, pa, "load");
    }
}

bool ArmRoutedAccess::Store(ArmCpuState* cpu_state, uint32_t guest_pc,
                            uint32_t va, uint32_t bytes, uint32_t value) {
    if ((va & (bytes - 1u)) != 0u) {
        uint32_t stored = value;
        if (mmu_->AccessPaged(cpu_state, va,
                              reinterpret_cast<uint8_t*>(&stored), bytes,
                              false)) {
            return true;
        }
        const uint32_t split_pa = mmu_->io_pending_address();
        if (split_pa == 0u) {
            return false;
        }
        HaltUnalignedRouted(guest_pc, va, bytes, split_pa, "store");
    }

    uint8_t* host = walker_->TranslateWrite(cpu_state, va);
    if (host != nullptr) {
        std::memcpy(host, &value, bytes);
        return true;
    }
    const uint32_t pa = mmu_->io_pending_address();
    if (pa == 0u) {
        return false;
    }
    switch (bytes) {
    case 1u: dispatcher_->WriteByte(pa, static_cast<uint8_t>(value)); return true;
    case 2u: dispatcher_->WriteHalf(pa, static_cast<uint16_t>(value)); return true;
    case 4u: dispatcher_->WriteWord(pa, value); return true;
    default: HaltRoutedWidth(guest_pc, va, bytes, pa, "store");
    }
}
