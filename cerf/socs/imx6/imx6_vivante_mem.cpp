#include "imx6_vivante_mem.h"

namespace imx6_vivante {

void VivanteMem::StoreStateReg(uint32_t byte_off, uint32_t value) {
    state_registers_.Store(byte_off, value);
}

const uint8_t* VivanteMem::TranslateGpuToHost(uint32_t gpu_addr, size_t size,
                                              MmuClient client) const {
    return mmu_.TranslateToHost(gpu_addr, size, client);
}

uint8_t* VivanteMem::TranslateGpuToHostWrite(uint32_t gpu_addr, size_t size,
                                             MmuClient client) const {
    return mmu_.TranslateToHostWrite(gpu_addr, size, client);
}

bool VivanteMem::DetectIdleRing(uint32_t pc, FeCommandAddressSpace address_space, IdleRingInfo& info) const {
    uint32_t pair[2]{};
    if (!ReadCommandWords(pc, pair, 2u, address_space)) return false;

    const uint32_t op = pair[0] >> 27;
    if (op == kFeWait) {
        uint32_t link[2]{};
        if (!ReadCommandWords(pc + 8u, link, 2u, address_space)) return false;
        if ((link[0] >> 27) != kFeLink || (link[0] & kFeCommandPrefetchMask) < 2u) {
            return false;
        }
        if (link[1] != pc && link[1] != pc + 8u) return false;
        info.base = pc;
        info.target = pc;
        info.address_space = address_space;
        return true;
    }

    if (op != kFeLink || pair[1] == 0u || (pair[0] & kFeCommandPrefetchMask) < 2u) {
        return false;
    }

    uint32_t linked[4]{};
    if (!ReadCommandWords(pair[1], linked, 4u, FeCommandAddressSpace::Virtual)) {
        return false;
    }
    if ((linked[0] >> 27) != kFeWait || (linked[2] >> 27) != kFeLink || (linked[2] & kFeCommandPrefetchMask) < 2u ||
        (linked[3] != pair[1] && linked[3] != pc)) {
        return false;
    }
    info.base = pair[1];
    info.target = pair[1];
    info.address_space = FeCommandAddressSpace::Virtual;
    return true;
}

const uint8_t* VivanteMem::TranslateCommandToHost(uint32_t address, size_t size,
                                                  FeCommandAddressSpace address_space) const {
    if (address_space == FeCommandAddressSpace::Virtual) return TranslateGpuToHost(address, size, MmuClient::Fe);

    return emu_.Get<EmulatedMemory>().TryTranslateRange(address, size);
}

bool VivanteMem::ReadCommandBytes(uint32_t address, void* out_buffer, size_t count,
                                  FeCommandAddressSpace address_space) const {
    auto* out = static_cast<uint8_t*>(out_buffer);
    if (!out && count != 0u) return false;
    if (count == 0u) return true;

    uint32_t last_touched = address;
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        const size_t chunk = count < page_left ? count : page_left;
        const uint8_t* src = TranslateCommandToHost(address, chunk, address_space);
        if (!src) return false;
        std::memcpy(out, src, chunk);
        last_touched = address + static_cast<uint32_t>(chunk - 1u);
        out += chunk;
        count -= chunk;
        if (count == 0u) break;
        if (chunk > 0xFFFFFFFFu - address) return false;
        address += static_cast<uint32_t>(chunk);
    }

    const uint32_t fetch = last_touched & ~7u;
    uint32_t pair[2]{};
    const uint8_t* lo = TranslateCommandToHost(fetch, sizeof(uint32_t), address_space);
    const uint8_t* hi = TranslateCommandToHost(fetch + 4u, sizeof(uint32_t), address_space);
    if (lo && hi) {
        std::memcpy(&pair[0], lo, sizeof(pair[0]));
        std::memcpy(&pair[1], hi, sizeof(pair[1]));
        s_.regs_[0x668u >> 2] = pair[0];
        s_.regs_[0x66Cu >> 2] = pair[1];
    }
    return true;
}

bool VivanteMem::ReadCommandWords(uint32_t address, uint32_t* out, uint32_t count,
                                  FeCommandAddressSpace address_space) const {
    if (!out && count != 0u) return false;
    return ReadCommandBytes(address, out, static_cast<size_t>(count) * sizeof(uint32_t), address_space);
}

bool VivanteMem::ReadMemoryWords(uint32_t address, uint32_t* out, uint32_t count) const {
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = TranslateGpuToHost(address + i * 4u, sizeof(out[i]), MmuClient::Fe);
        if (!p) return false;
        std::memcpy(&out[i], p, sizeof(out[i]));
    }
    return true;
}

bool VivanteMem::ReadGpuBytes(uint32_t address, void* out_buffer, size_t count, MmuClient client) const {
    auto* out = static_cast<uint8_t*>(out_buffer);
    if ((!out && count != 0u)) return false;
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        size_t chunk = count;
        if (chunk > page_left) chunk = page_left;
        const uint8_t* src = TranslateGpuToHost(address, chunk, client);
        if (!src) return false;
        std::memcpy(out, src, chunk);
        out += chunk;
        count -= chunk;
        if (count == 0u) break;
        if (chunk > 0xFFFFFFFFu - address) return false;
        address += static_cast<uint32_t>(chunk);
    }
    return true;
}

bool VivanteMem::WriteGpuBytes(uint32_t address, const void* in_buffer, size_t count, MmuClient client) const {
    const auto* in = static_cast<const uint8_t*>(in_buffer);
    if ((!in && count != 0u)) return false;
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        size_t chunk = count;
        if (chunk > page_left) chunk = page_left;
        uint8_t* dst = TranslateGpuToHostWrite(address, chunk, client);
        if (!dst) return false;
        std::memcpy(dst, in, chunk);
        in += chunk;
        count -= chunk;
        if (count == 0u) break;
        if (chunk > 0xFFFFFFFFu - address) return false;
        address += static_cast<uint32_t>(chunk);
    }
    return true;
}

bool VivanteMem::ReadMemoryU64(uint32_t address, uint64_t& out) const {
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    if (!ReadMemoryWords(address, &lo, 1u) || !ReadMemoryWords(address + 4u, &hi, 1u)) return false;
    out = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
    return true;
}

}
