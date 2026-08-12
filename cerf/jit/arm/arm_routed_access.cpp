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

uint32_t ArmRoutedAccess::DispatchRead(uint32_t pa, uint32_t bytes,
                                       uint32_t guest_pc, uint32_t va) {
    switch (bytes) {
    case 1u: return dispatcher_->ReadByte(pa);
    case 2u: return dispatcher_->ReadHalf(pa);
    case 4u: return dispatcher_->ReadWord(pa);
    default: HaltRoutedWidth(guest_pc, va, bytes, pa, "load");
    }
}

void ArmRoutedAccess::DispatchWrite(uint32_t pa, uint32_t bytes, uint32_t value,
                                    uint32_t guest_pc, uint32_t va) {
    switch (bytes) {
    case 1u: dispatcher_->WriteByte(pa, static_cast<uint8_t>(value)); return;
    case 2u: dispatcher_->WriteHalf(pa, static_cast<uint16_t>(value)); return;
    case 4u: dispatcher_->WriteWord(pa, value); return;
    default: HaltRoutedWidth(guest_pc, va, bytes, pa, "store");
    }
}

uint32_t ArmRoutedAccess::IoLoadHelper(ArmRoutedAccess* self, uint32_t bytes,
                                       uint32_t guest_pc, uint32_t va) {
    const uint32_t pa = self->mmu_->io_pending_address();
    self->mmu_->ClearIoPending();
    if ((pa & (bytes - 1u)) != 0u) {
        self->HaltUnalignedRouted(guest_pc, va, bytes, pa, "load");
    }
    return self->DispatchRead(pa, bytes, guest_pc, va);
}

void ArmRoutedAccess::IoStoreHelper(ArmRoutedAccess* self, uint32_t bytes,
                                    uint32_t guest_pc, uint32_t va,
                                    uint32_t value) {
    const uint32_t pa = self->mmu_->io_pending_address();
    self->mmu_->ClearIoPending();
    if ((pa & (bytes - 1u)) != 0u) {
        self->HaltUnalignedRouted(guest_pc, va, bytes, pa, "store");
    }
    self->DispatchWrite(pa, bytes, value, guest_pc, va);
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
        if (!mmu_->io_pending()) {
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
    if (!mmu_->io_pending()) {
        return false;
    }
    *out = DispatchRead(pa, bytes, guest_pc, va);
    return true;
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
        if (!mmu_->io_pending()) {
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
    if (!mmu_->io_pending()) {
        return false;
    }
    DispatchWrite(pa, bytes, value, guest_pc, va);
    return true;
}
