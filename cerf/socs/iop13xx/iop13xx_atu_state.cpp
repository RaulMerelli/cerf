#include "iop13xx_atu_state.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../state/state_stream.h"

REGISTER_SERVICE(Iop13xxAtuState);

bool Iop13xxAtuState::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSoc() == SocFamily::IOP13xx;
}

bool Iop13xxAtuState::OutboundGloballyEnabled() const {
    return (atucr_ & kAtucrOutboundEnable) != 0;
}

bool Iop13xxAtuState::BusMasterEnabled() const {
    return (atucmd_ & kAtucmdBusMasterEnable) != 0;
}

bool Iop13xxAtuState::OutboundMemoryWindowEnabled(uint32_t idx) const {
    return idx < kMemoryWindowCount && (oum_[idx].bar & kOutboundWindowEnable) != 0;
}

bool Iop13xxAtuState::RangeFitsLow32(uint64_t addr, uint32_t size) {
    if (size == 0) return false;
    const uint64_t low = addr & 0xFFFFFFFFull;
    return low + static_cast<uint64_t>(size) - 1ull <= 0xFFFFFFFFull;
}

bool Iop13xxAtuState::RangeFitsLow16(uint32_t addr, uint32_t size) {
    if (size == 0) return false;
    const uint32_t low = addr & 0xFFFFu;
    return low + size - 1u <= 0xFFFFu;
}

bool Iop13xxAtuState::CpuPhysToPciMemBus(uint64_t cpu_pa, uint32_t size, uint64_t& pci_bus_addr, uint32_t* window_idx,
                                         bool require_enabled) const {
    if (!RangeFitsLow32(cpu_pa, size)) return false;
    if (require_enabled &&
        (!OutboundGloballyEnabled() || ((atucmd_ & kAtucmdMemorySpaceEnable) == 0) || !BusMasterEnabled()))
        return false;

    const uint32_t section = static_cast<uint32_t>((cpu_pa >> 32) & 0xFu);
    for (uint32_t i = 0; i < kMemoryWindowCount; ++i) {
        const auto& w = oum_[i];
        if (require_enabled && !OutboundMemoryWindowEnabled(i)) continue;
        if ((w.bar & kOutboundWindowSectionMask) != section) continue;
        pci_bus_addr = (static_cast<uint64_t>(w.wtvr) << 32) | static_cast<uint32_t>(cpu_pa);
        if (window_idx) *window_idx = i;
        return true;
    }
    return false;
}

bool Iop13xxAtuState::PciMemBusToCpuPhys(uint64_t pci_bus_addr, uint32_t size, uint64_t& cpu_pa, uint32_t* window_idx,
                                         bool require_enabled) const {
    if (!RangeFitsLow32(pci_bus_addr, size)) return false;
    if (require_enabled &&
        (!OutboundGloballyEnabled() || ((atucmd_ & kAtucmdMemorySpaceEnable) == 0) || !BusMasterEnabled()))
        return false;

    const uint32_t pci_hi = static_cast<uint32_t>(pci_bus_addr >> 32);
    const uint32_t pci_lo = static_cast<uint32_t>(pci_bus_addr);
    for (uint32_t i = 0; i < kMemoryWindowCount; ++i) {
        const auto& w = oum_[i];
        if (require_enabled && !OutboundMemoryWindowEnabled(i)) continue;
        if (w.wtvr != pci_hi) continue;
        cpu_pa = (static_cast<uint64_t>(w.bar & kOutboundWindowSectionMask) << 32) | pci_lo;
        if (window_idx) *window_idx = i;
        return true;
    }
    return false;
}

bool Iop13xxAtuState::CpuPhysToPciIoBus(uint64_t cpu_pa, uint32_t size, uint32_t& pci_io_addr,
                                        bool require_enabled) const {
    if (!RangeFitsLow16(static_cast<uint32_t>(cpu_pa), size)) return false;
    if (require_enabled && (!OutboundGloballyEnabled() || !BusMasterEnabled())) return false;
    const uint64_t base = static_cast<uint64_t>(oiobar_ & 0xFFFF0000u);
    if (cpu_pa < base || cpu_pa + size - 1ull >= base + 0x10000ull) return false;
    pci_io_addr = (oiowtvr_ & 0xFFFF0000u) | static_cast<uint32_t>(cpu_pa & 0xFFFFu);
    return true;
}

bool Iop13xxAtuState::PciIoBusToCpuPhys(uint32_t pci_io_addr, uint32_t size, uint64_t& cpu_pa,
                                        bool require_enabled) const {
    if (!RangeFitsLow16(pci_io_addr, size)) return false;
    if (require_enabled && (!OutboundGloballyEnabled() || !BusMasterEnabled())) return false;
    if ((pci_io_addr & 0xFFFF0000u) != (oiowtvr_ & 0xFFFF0000u)) return false;
    cpu_pa = static_cast<uint64_t>(oiobar_ & 0xFFFF0000u) | static_cast<uint64_t>(pci_io_addr & 0xFFFFu);
    return true;
}

void Iop13xxAtuState::SaveCoreState(StateWriter& w) const {
    w.Write(atucmd_);
    w.Write(atusr_);
    w.Write(atucr_);
    w.Write(atuisr_);
    w.Write(atuimr_);
}

void Iop13xxAtuState::RestoreCoreState(StateReader& r) {
    r.Read(atucmd_);
    r.Read(atusr_);
    r.Read(atucr_);
    r.Read(atuisr_);
    r.Read(atuimr_);
}

void Iop13xxAtuState::SaveOutboundState(StateWriter& w) const {
    w.Write(oiobar_);
    w.Write(oiowtvr_);
    for (const auto& win : oum_) {
        w.Write(win.bar);
        w.Write(win.wtvr);
    }
}

void Iop13xxAtuState::RestoreOutboundState(StateReader& r) {
    r.Read(oiobar_);
    r.Read(oiowtvr_);
    for (auto& win : oum_) {
        r.Read(win.bar);
        r.Read(win.wtvr);
    }
}
