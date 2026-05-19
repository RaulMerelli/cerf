#include "peripheral_dispatcher.h"

#include "peripheral_base.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../core/rate_probe.h"
#include "../cpu/emulated_memory.h"
#include "../jit/arm_mmu.h"

#include <algorithm>
#include <intrin.h>

REGISTER_SERVICE(PeripheralDispatcher);

void PeripheralDispatcher::OnReady() {
    mmu_ = &emu_.Get<ArmMmu>();
}

void PeripheralDispatcher::Register(Peripheral* p) {
    if (!p) {
        LOG(Caution, "PeripheralDispatcher::Register called with null\n");
        CerfFatalExit(1);
    }
    const uint32_t base = p->MmioBase();
    const uint32_t size = p->MmioSize();
    if (size == 0) {
        LOG(Caution, "PeripheralDispatcher::Register peripheral has "
                "zero-size MMIO range (base 0x%08X)\n", base);
        CerfFatalExit(1);
    }

    std::lock_guard<std::mutex> lk(io_lock_);

    for (const auto& e : entries_) {
        if (base < e.base + e.size && e.base < base + size) {
            LOG(Caution, "PeripheralDispatcher::Register overlap: "
                    "new [0x%08X..0x%08X) vs existing [0x%08X..0x%08X)\n",
                    base, base + size, e.base, e.base + e.size);
            CerfFatalExit(1);
        }
    }

    Entry entry{base, size, p};
    auto pos = std::lower_bound(entries_.begin(), entries_.end(), base,
        [](const Entry& e, uint32_t b) { return e.base < b; });
    entries_.insert(pos, entry);

    if (entries_.size() > 128u) {
        LOG(Caution, "PeripheralDispatcher::Register: more than 128 peripherals "
                "registered — the JIT IO helper's per-emit-site cache slot is a "
                "signed int8 holding the entries_ array index; index space "
                "exhausted.\n");
        CerfFatalExit(1);
    }

    LOG(Periph, "Register 0x%08X size 0x%X\n", base, size);
}

bool PeripheralDispatcher::IsPeripheralAddress(uint32_t addr) const {
    std::lock_guard<std::mutex> lk(io_lock_);
    return Lookup(addr) != nullptr;
}

Peripheral* PeripheralDispatcher::Lookup(uint32_t addr) const {
    Entry* e = LookupEntry(addr);
    return e ? e->p : nullptr;
}

PeripheralDispatcher::Entry* PeripheralDispatcher::LookupEntry(uint32_t addr) const {
    /* upper_bound finds the first entry whose base > addr. The
       candidate covering addr is the one immediately before it
       (largest base ≤ addr). */
    auto it = std::upper_bound(entries_.begin(), entries_.end(), addr,
        [](uint32_t a, const Entry& e) { return a < e.base; });
    if (it == entries_.begin()) return nullptr;
    --it;
    if (addr >= it->base && addr < it->base + it->size) {
        return const_cast<Entry*>(&(*it));
    }
    return nullptr;
}

uint8_t PeripheralDispatcher::ReadByte(uint32_t addr) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); return p->ReadByte(addr); }
    lk.unlock();
    return emu_.Get<EmulatedMemory>().ReadByte(addr);
}

uint16_t PeripheralDispatcher::ReadHalf(uint32_t addr) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); return p->ReadHalf(addr); }
    lk.unlock();
    return emu_.Get<EmulatedMemory>().ReadHalf(addr);
}

uint32_t PeripheralDispatcher::ReadWord(uint32_t addr) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); return p->ReadWord(addr); }
    lk.unlock();
    return emu_.Get<EmulatedMemory>().ReadWord(addr);
}

uint64_t PeripheralDispatcher::ReadDword(uint32_t addr) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); return p->ReadDword(addr); }
    lk.unlock();
    return emu_.Get<EmulatedMemory>().ReadDword(addr);
}

void PeripheralDispatcher::WriteByte(uint32_t addr, uint8_t value) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); p->WriteByte(addr, value); return; }
    lk.unlock();
    emu_.Get<EmulatedMemory>().WriteByte(addr, value);
}

void PeripheralDispatcher::WriteHalf(uint32_t addr, uint16_t value) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); p->WriteHalf(addr, value); return; }
    lk.unlock();
    emu_.Get<EmulatedMemory>().WriteHalf(addr, value);
}

void PeripheralDispatcher::WriteWord(uint32_t addr, uint32_t value) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); p->WriteWord(addr, value); return; }
    lk.unlock();
    emu_.Get<EmulatedMemory>().WriteWord(addr, value);
}

void PeripheralDispatcher::WriteDword(uint32_t addr, uint64_t value) {
    std::unique_lock<std::mutex> lk(io_lock_);
    if (Peripheral* p = Lookup(addr)) { lk.unlock(); p->WriteDword(addr, value); return; }
    lk.unlock();
    emu_.Get<EmulatedMemory>().WriteDword(addr, value);
}

uint8_t __fastcall PeripheralDispatcher::JitIoReadByte(int8_t* hint, PeripheralDispatcher* d) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_read_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        const uint8_t result = static_cast<uint8_t>(
            d->fast_read_(d->fast_ctx_, addr - d->fast_base_, 1));
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return result;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoReadByte: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    const uint8_t result = entry->p->ReadByte(addr);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
    return result;
}

uint16_t __fastcall PeripheralDispatcher::JitIoReadHalf(int8_t* hint, PeripheralDispatcher* d) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_read_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        const uint16_t result = static_cast<uint16_t>(
            d->fast_read_(d->fast_ctx_, addr - d->fast_base_, 2));
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return result;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoReadHalf: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    const uint16_t result = entry->p->ReadHalf(addr);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
    return result;
}

uint32_t __fastcall PeripheralDispatcher::JitIoReadWord(int8_t* hint, PeripheralDispatcher* d) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_read_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        const uint32_t result = d->fast_read_(d->fast_ctx_, addr - d->fast_base_, 4);
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return result;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoReadWord: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    const uint32_t result = entry->p->ReadWord(addr);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
    return result;
}

void __fastcall PeripheralDispatcher::JitIoWriteByte(int8_t* hint, PeripheralDispatcher* d, uint8_t value) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_write_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        d->fast_write_(d->fast_ctx_, addr - d->fast_base_, value, 1);
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoWriteByte: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    entry->p->WriteByte(addr, value);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
}

void __fastcall PeripheralDispatcher::JitIoWriteHalf(int8_t* hint, PeripheralDispatcher* d, uint16_t value) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_write_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        d->fast_write_(d->fast_ctx_, addr - d->fast_base_, value, 2);
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoWriteHalf: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    entry->p->WriteHalf(addr, value);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
}

void __fastcall PeripheralDispatcher::JitIoWriteWord(int8_t* hint, PeripheralDispatcher* d, uint32_t value) {
#if CERF_DEV_MODE
    const uint64_t t0 = __rdtsc();
#endif
    const uint32_t addr = d->mmu_->io_pending_address() +
                          d->mmu_->io_pending_address_adjust();

    if (d->fast_write_ && addr >= d->fast_base_ && addr < d->fast_end_) {
        d->fast_write_(d->fast_ctx_, addr - d->fast_base_, value, 4);
#if CERF_DEV_MODE
        auto& probe = d->emu_.Get<RateProbe>();
        probe.RecordMmioPc(d->last_guest_pc_, addr);
        probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
        return;
    }

    const int8_t cached_index = *hint;
    Entry* entry = &d->entries_[cached_index];

    if (addr < entry->base || addr >= entry->base + entry->size) {
        entry = d->LookupEntry(addr);
        if (!entry) {
            LOG(Caution, "PeripheralDispatcher::JitIoWriteWord: no peripheral "
                    "registered at 0x%08X\n", addr);
            CerfFatalExit(2);
        }
        *hint = static_cast<int8_t>(entry - d->entries_.data());
    }

    std::lock_guard<std::mutex> lk(d->io_lock_);
    entry->p->WriteWord(addr, value);
#if CERF_DEV_MODE
    auto& probe = d->emu_.Get<RateProbe>();
    probe.RecordMmioPc(d->last_guest_pc_, addr);
    probe.AddTsc(RateProbe::TimeCounter::JitIo, __rdtsc() - t0);
#endif
}
