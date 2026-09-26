#include "imx6_vivante_mmu.h"

#include "../../core/cerf_emulator.h"
#include "../../cpu/emulated_memory.h"

#include <cstring>

namespace imx6_vivante {

uint32_t VivanteMmu::PageTableRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1FePageTable, kMmuv1TxPageTable,
        kMmuv1PePageTable, kMmuv1RaPageTable,
    };
    return registers[static_cast<uint32_t>(client)];
}

uint32_t VivanteMmu::MemoryBaseRegister(MmuClient client) {
    static constexpr uint32_t registers[] = {
        kMmuv1MemoryBaseFe, kMmuv1MemoryBaseTx,
        kMmuv1MemoryBasePe, kMmuv1MemoryBaseRa,
    };
    return registers[static_cast<uint32_t>(client)];
}
bool VivanteMmu::ReadPhysicalU32(uint32_t address, uint32_t& value) const {
    const uint8_t* entry = emu_.Get<EmulatedMemory>().TryTranslate(address);
    if (!entry) return false;
    std::memcpy(&value, entry, sizeof(value));
    return true;
}

bool VivanteMmu::TranslateMmuv1(uint32_t gpu_address, MmuClient client,
                                uint32_t& physical) const {
    if (gpu_address < kMmuv1GpuMemStart) {
        physical = state_.regs_[MemoryBaseRegister(client) >> 2] + gpu_address;
        return true;
    }

    /* Linux etnaviv_iommu.c::etnaviv_iommuv1_map uses raw 4 KiB PA entries
       above GPU_MEM_START; state_hi.xml MC defines the per-client table bases. */
    const uint32_t page = (gpu_address - kMmuv1GpuMemStart) >> 12;
    const uint32_t table = state_.regs_[PageTableRegister(client) >> 2];
    uint32_t pte = 0u;
    if (!ReadPhysicalU32(table + page * 4u, pte)) return false;
    physical = (pte & kMmuv1PageMask) | (gpu_address & ~kMmuv1PageMask);
    return true;
}
const uint8_t* VivanteMmu::TranslateToHost(uint32_t gpu_address,
                                           MmuClient client) const {
    uint32_t physical = 0u;
    if (!TranslateMmuv1(gpu_address, client, physical)) return nullptr;
    return emu_.Get<EmulatedMemory>().TryTranslate(physical);
}

uint8_t* VivanteMmu::TranslateToHostWrite(uint32_t gpu_address,
                                          MmuClient client) const {
    uint32_t physical = 0u;
    if (!TranslateMmuv1(gpu_address, client, physical)) return nullptr;
    return emu_.Get<EmulatedMemory>().TryTranslateWrite(physical);
}

}
