#include "imx6_vivante_mem.h"

namespace imx6_vivante {

bool VivanteMem::DetectIdleRing(uint32_t pc, FeCommandAddressSpace address_space,
                                IdleRingInfo& info) const {
    uint32_t pair[2]{};
    if (!ReadCommandWords(pc, pair, 2u, address_space))
        return false;

    const uint32_t op = pair[0] >> 27;
    if (op == kFeWait) {
        uint32_t link[2]{};
        if (!ReadCommandWords(pc + 8u, link, 2u, address_space))
            return false;
        if ((link[0] >> 27) != kFeLink ||
            (link[0] & kFeCommandPrefetchMask) < 2u) {
            return false;
        }
        if (link[1] != pc && link[1] != pc + 8u)
            return false;
        info.base = pc;
        info.target = pc;
        info.address_space = address_space;
        return true;
    }

    if (op != kFeLink || pair[1] == 0u ||
        (pair[0] & kFeCommandPrefetchMask) < 2u) {
        return false;
    }

    /* LINK.ADDRESS is VIVM.  Recognize a command-buffer tail that returns
       to the canonical WAIT/pad/LINK idle loop without scanning around the
       decoder PC or interpreting a WAIT padding word as queued work. */
    uint32_t linked[4]{};
    if (!ReadCommandWords(pair[1], linked, 4u,
                          FeCommandAddressSpace::Virtual)) {
        return false;
    }
    if ((linked[0] >> 27) != kFeWait ||
        (linked[2] >> 27) != kFeLink ||
        (linked[2] & kFeCommandPrefetchMask) < 2u ||
        (linked[3] != pair[1] && linked[3] != pc)) {
        return false;
    }
    info.base = pair[1];
    info.target = pair[1];
    info.address_space = FeCommandAddressSpace::Virtual;
    return true;
}

const uint8_t* VivanteMem::TranslateCommandToHost(
    uint32_t address, FeCommandAddressSpace address_space) const {
    if (address_space == FeCommandAddressSpace::Virtual)
        return TranslateGpuToHost(address, MmuClient::Fe);

    /* VIVS_FE_COMMAND_ADDRESS is explicitly always physical.  Do not run
       the bootstrap/idle-ring address through MMUv2. */
    return emu_.Get<EmulatedMemory>().TryTranslate(address);
}

bool VivanteMem::ReadCommandBytes(uint32_t address, void* out_buffer,
                                  size_t count,
                                  FeCommandAddressSpace address_space) const {
    auto* out = static_cast<uint8_t*>(out_buffer);
    if (!out && count != 0u)
        return false;
    if (count == 0u)
        return true;

    uint32_t last_touched = address;
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        const size_t chunk = count < page_left ? count : page_left;
        const uint8_t* src = TranslateCommandToHost(address, address_space);
        if (!src)
            return false;
        std::memcpy(out, src, chunk);
        last_touched = address + static_cast<uint32_t>(chunk - 1u);
        out += chunk;
        count -= chunk;
        if (count == 0u)
            break;
        if (chunk > 0xFFFFFFFFu - address)
            return false;
        address += static_cast<uint32_t>(chunk);
    }

    /* The FE fetches 64-bit words.  DMA_LOW/HIGH expose the last fetched
       pair; DMA_ADDRESS remains the decoder PC. */
    const uint32_t fetch = last_touched & ~7u;
    uint32_t pair[2]{};
    const uint8_t* lo = TranslateCommandToHost(fetch, address_space);
    const uint8_t* hi = TranslateCommandToHost(fetch + 4u, address_space);
    if (lo && hi) {
        std::memcpy(&pair[0], lo, sizeof(pair[0]));
        std::memcpy(&pair[1], hi, sizeof(pair[1]));
        s_.regs_[0x668u >> 2] = pair[0];
        s_.regs_[0x66Cu >> 2] = pair[1];
    }
    return true;
}

bool VivanteMem::ReadCommandWords(uint32_t address, uint32_t* out,
                                  uint32_t count,
                                  FeCommandAddressSpace address_space) const {
    if (!out && count != 0u)
        return false;
    return ReadCommandBytes(address, out,
                            static_cast<size_t>(count) * sizeof(uint32_t),
                            address_space);
}

bool VivanteMem::ReadMemoryWords(uint32_t address, uint32_t* out,
                                 uint32_t count) const {
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = TranslateGpuToHost(
            address + i * 4u, MmuClient::Fe);
        if (!p)
            return false;
        std::memcpy(&out[i], p, sizeof(out[i]));
    }
    return true;
}

bool VivanteMem::ReadGpuBytes(uint32_t address, void* out_buffer, size_t count,
                              MmuClient client) const {
    auto* out = static_cast<uint8_t*>(out_buffer);
    if ((!out && count != 0u))
        return false;
    while (count != 0u) {
        /* A translated host pointer is valid only inside the current GPU
           page.  SAFE_ADDRESS is a 64-byte window, so also split there;
           this keeps redirected reads inside the documented aperture. */
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        const size_t safe_left = 0x40u - (address & 0x3Fu);
        size_t chunk = count;
        if (chunk > page_left) chunk = page_left;
        if (chunk > safe_left) chunk = safe_left;
        const uint8_t* src = TranslateGpuToHost(address, client);
        if (!src)
            return false;
        std::memcpy(out, src, chunk);
        out += chunk;
        count -= chunk;
        if (count == 0u)
            break;
        if (chunk > 0xFFFFFFFFu - address)
            return false;
        address += static_cast<uint32_t>(chunk);
    }
    return true;
}

bool VivanteMem::WriteGpuBytes(uint32_t address, const void* in_buffer,
                               size_t count, MmuClient client) const {
    const auto* in = static_cast<const uint8_t*>(in_buffer);
    if ((!in && count != 0u))
        return false;
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        size_t chunk = count;
        if (chunk > page_left) chunk = page_left;
        uint8_t* dst = TranslateGpuToHostWrite(address, client);
        if (!dst)
            return false;
        std::memcpy(dst, in, chunk);
        in += chunk;
        count -= chunk;
        if (count == 0u)
            break;
        if (chunk > 0xFFFFFFFFu - address)
            return false;
        address += static_cast<uint32_t>(chunk);
    }
    return true;
}

bool VivanteMem::ReadMemoryU64(uint32_t address, uint64_t& out) const {
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    /* Translate each word independently so a fence at the end of a GPU
       page does not rely on host contiguity across two PTEs. */
    if (!ReadMemoryWords(address, &lo, 1u) ||
        !ReadMemoryWords(address + 4u, &hi, 1u))
        return false;
    out = static_cast<uint64_t>(lo) |
          (static_cast<uint64_t>(hi) << 32);
    return true;
}

void VivanteMem::DumpCommandWords(uint32_t address, uint32_t prefetch,
                                  FeCommandAddressSpace address_space) const {
    const uint32_t prefetch_words = prefetch * 2u;
    const uint32_t words = prefetch
        ? ((prefetch_words < 32u) ? prefetch_words : 32u)
        : 0u;
    if (words == 0u)
        return;

    char line[320];
    int n = std::snprintf(line, sizeof(line),
                          "Imx6Gpu%s: FE words @0x%08X:",
                          CoreName(), address);
    for (uint32_t i = 0; i < words && n > 0 &&
         n < static_cast<int>(sizeof(line)) - 16; ++i) {
        uint32_t w = 0u;
        if (!ReadCommandWords(address + i * 4u, &w, 1u,
                              address_space))
            break;
        n += std::snprintf(line + n, sizeof(line) - n, " %08X", w);
    }
}

}  // namespace imx6_vivante
