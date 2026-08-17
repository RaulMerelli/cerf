#include "peripheral_dispatcher.h"

#include "peripheral_base.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../socs/iop13xx/iop13xx_atu.h"

#include <algorithm>
#include <limits>
#include <typeinfo>

REGISTER_SERVICE(PeripheralDispatcher);

std::vector<Peripheral*> PeripheralDispatcher::RegisteredPeripherals() const {
    std::vector<Peripheral*> out;
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return out;
    out.reserve(t->size());
    for (const auto& e : *t) {
        if (std::find(out.begin(), out.end(), e.p) == out.end()) {
            out.push_back(e.p);
        }
    }
    return out;
}

void PeripheralDispatcher::Register(Peripheral* p) {
    if (!p) {
        LOG(Caution, "PeripheralDispatcher::Register called with null\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    RegisterAlias(p, p->MmioBase(), p->MmioSize(), p->FastReader(),
                  p->FastWriter(), p);
}

void PeripheralDispatcher::RegisterAlias(Peripheral* owner, uint64_t base,
                                          uint64_t size,
                                          Peripheral::FastReadFn read,
                                          Peripheral::FastWriteFn write,
                                          void* ctx) {
    if (!owner || !read || !write || !ctx) {
        LOG(Caution, "PeripheralDispatcher::RegisterAlias called with null argument\n");
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint64_t end = base + size;
    if (size == 0 || end <= base || size > (std::numeric_limits<uint32_t>::max)()) {
        LOG(Caution, "PeripheralDispatcher::RegisterAlias invalid MMIO range: "
                     "base=0x%016llX size=0x%016llX\n",
            static_cast<unsigned long long>(base),
            static_cast<unsigned long long>(size));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    const EntryTable* prev = live_.load(std::memory_order_acquire);
    if (prev) {
        for (const auto& e : *prev) {
            if (base < e.end && e.base < end) {
                LOG(Caution, "PeripheralDispatcher::RegisterAlias overlap: "
                        "new [0x%016llX..0x%016llX) vs existing "
                        "[0x%016llX..0x%016llX)\n",
                        static_cast<unsigned long long>(base),
                        static_cast<unsigned long long>(end),
                        static_cast<unsigned long long>(e.base),
                        static_cast<unsigned long long>(e.end));
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
        }
    }

    auto next = prev ? std::make_unique<EntryTable>(*prev)
                     : std::make_unique<EntryTable>();
    Entry entry{base, end, read, write, ctx, owner};
    auto pos = std::lower_bound(next->begin(), next->end(), base,
        [](const Entry& e, uint64_t b) { return e.base < b; });
    next->insert(pos, entry);

    const EntryTable* published = next.get();
    tables_.push_back(std::move(next));
    last_hit_.store(0, std::memory_order_relaxed);
    live_.store(published, std::memory_order_release);

    LOG(Periph, "Register 0x%016llX..0x%016llX\n",
        static_cast<unsigned long long>(base),
        static_cast<unsigned long long>(end));
}

bool PeripheralDispatcher::IsPeripheralAddress(uint32_t addr) const {
    return LookupEntry(ResolveAtuOutboundAlias(addr, 1u)) != nullptr;
}

void PeripheralDispatcher::ValidatePhysReachable(uint32_t phys_addr_mask) const {
    if (phys_addr_mask == 0xFFFFFFFFu) return;
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return;
    for (const auto& e : *t) {
        if ((e.base >> 32) != 0u || (e.end >> 32) != 0u) continue;
        const uint32_t base = static_cast<uint32_t>(e.base);
        const uint32_t end = static_cast<uint32_t>(e.end);
        if ((end - 1u) > phys_addr_mask) {
            LOG(Caution, "PeripheralDispatcher: %s at [0x%08X..0x%08X) is above "
                    "the SoC physical space (mask 0x%08X); it aliases to "
                    "0x%08X and is unreachable/shadowed - relocate it into the "
                    "addressable range\n",
                    typeid(*e.p).name(), base, end, phys_addr_mask,
                    base & phys_addr_mask);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
    }
}

/* QEMU system/physmem.c:345 address_space_lookup_region(), mru_section. */
const PeripheralDispatcher::Entry* PeripheralDispatcher::LookupEntry(
    uint64_t addr) const {
    if (const Entry* hit = MemoHit(addr)) return hit;
    return LookupSlow(addr);
}

const PeripheralDispatcher::Entry* PeripheralDispatcher::LookupSlow(
    uint64_t addr) const {
    const EntryTable* t = live_.load(std::memory_order_acquire);
    if (!t) return nullptr;

    auto it = std::upper_bound(t->begin(), t->end(), addr,
        [](uint64_t a, const Entry& e) { return a < e.base; });
    if (it == t->begin()) return nullptr;
    --it;
    if (addr >= it->base && addr < it->end) {
        last_hit_.store(static_cast<size_t>(it - t->begin()),
                        std::memory_order_relaxed);
        return &(*it);
    }
    return nullptr;
}

const PeripheralDispatcher::Entry* PeripheralDispatcher::LookupRaw(
    uint64_t addr) const {
    return LookupSlow(addr);
}

uint64_t PeripheralDispatcher::ResolveAtuOutboundAlias(uint32_t addr,
                                                        uint32_t width) const {
    Iop13xxAtuState* atu = atu_;
    if (!atu) {
        atu = emu_.TryGet<Iop13xxAtuState>();
        atu_ = atu;
    }
    if (!atu) return addr;

    uint64_t cpu_pa = 0;
    if (!atu->PciMemBusToCpuPhys(addr, width, cpu_pa, nullptr) ||
        cpu_pa == addr || !LookupRaw(cpu_pa)) {
        return addr;
    }
    return cpu_pa;
}

uint32_t PeripheralDispatcher::ReadSlow(uint32_t raw_addr, uint64_t addr,
                                        MmioWidth width) {
    if (const Entry* e = LookupSlow(addr)) {
        return ClipToWidth(
            e->read(e->ctx, static_cast<uint32_t>(addr - e->base),
                    static_cast<uint32_t>(width)),
            width);
    }
    switch (width) {
    case MmioWidth::kByte: return emu_.Get<EmulatedMemory>().ReadByte(raw_addr);
    case MmioWidth::kHalf: return emu_.Get<EmulatedMemory>().ReadHalf(raw_addr);
    case MmioWidth::kWord: return emu_.Get<EmulatedMemory>().ReadWord(raw_addr);
    }
    HaltBadWidth(static_cast<uint32_t>(width));
}

void PeripheralDispatcher::HaltBadWidth(uint32_t width) {
    LOG(Caution, "PeripheralDispatcher: unmodeled MMIO width %u\n", width);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void PeripheralDispatcher::WriteSlow(uint32_t raw_addr, uint64_t addr,
                                     uint32_t value, MmioWidth width) {
    if (const Entry* e = LookupSlow(addr)) {
        e->write(e->ctx, static_cast<uint32_t>(addr - e->base),
                 ClipToWidth(value, width),
                 static_cast<uint32_t>(width));
        return;
    }
    switch (width) {
    case MmioWidth::kByte: emu_.Get<EmulatedMemory>().WriteByte(raw_addr, static_cast<uint8_t>(value)); return;
    case MmioWidth::kHalf: emu_.Get<EmulatedMemory>().WriteHalf(raw_addr, static_cast<uint16_t>(value)); return;
    case MmioWidth::kWord: emu_.Get<EmulatedMemory>().WriteWord(raw_addr, value); return;
    }
    HaltBadWidth(static_cast<uint32_t>(width));
}

uint8_t PeripheralDispatcher::ReadByte(uint32_t addr) {
    return static_cast<uint8_t>(Read(addr, MmioWidth::kByte));
}

uint16_t PeripheralDispatcher::ReadHalf(uint32_t addr) {
    return static_cast<uint16_t>(Read(addr, MmioWidth::kHalf));
}

uint32_t PeripheralDispatcher::ReadWord(uint32_t addr) {
    return Read(addr, MmioWidth::kWord);
}

uint64_t PeripheralDispatcher::ReadDword(uint32_t addr) {
    const uint64_t resolved = ResolveAtuOutboundAlias(addr, 8u);
    if (const Entry* e = LookupEntry(resolved)) {
        if (resolved > (std::numeric_limits<uint32_t>::max)() ||
            e->base != e->p->MmioBase()) {
            const uint32_t off = static_cast<uint32_t>(resolved - e->base);
            const uint64_t lo = e->read(e->ctx, off, 4u);
            const uint64_t hi = e->read(e->ctx, off + 4u, 4u);
            return lo | (hi << 32);
        }
        return e->p->ReadDword(addr);
    }
    return emu_.Get<EmulatedMemory>().ReadDword(addr);
}

void PeripheralDispatcher::WriteByte(uint32_t addr, uint8_t value) {
    Write(addr, value, MmioWidth::kByte);
}

void PeripheralDispatcher::WriteHalf(uint32_t addr, uint16_t value) {
    Write(addr, value, MmioWidth::kHalf);
}

void PeripheralDispatcher::WriteWord(uint32_t addr, uint32_t value) {
    Write(addr, value, MmioWidth::kWord);
}

void PeripheralDispatcher::WriteDword(uint32_t addr, uint64_t value) {
    const uint64_t resolved = ResolveAtuOutboundAlias(addr, 8u);
    if (const Entry* e = LookupEntry(resolved)) {
        if (resolved > (std::numeric_limits<uint32_t>::max)() ||
            e->base != e->p->MmioBase()) {
            const uint32_t off = static_cast<uint32_t>(resolved - e->base);
            e->write(e->ctx, off, static_cast<uint32_t>(value), 4u);
            e->write(e->ctx, off + 4u, static_cast<uint32_t>(value >> 32), 4u);
            return;
        }
        e->p->WriteDword(addr, value);
        return;
    }
    emu_.Get<EmulatedMemory>().WriteDword(addr, value);
}
