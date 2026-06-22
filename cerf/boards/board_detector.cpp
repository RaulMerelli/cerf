#define NOMINMAX

#include "board_detector.h"

#include "../boot/rom_parser_service.h"
#include "../boot/sec_flash.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"

#include <algorithm>
#include <cstring>
#include <vector>

void BoardDetector::OnReady() {
    LOG(Board, "detected board: %s (SoC %s)\n",
        BoardName(), SocFamilyName(GetSoc()));
}

const char* BoardDetector::SocFamilyName(SocFamily f) {
    switch (f) {
        case SocFamily::Unknown:   return "Unknown";
        case SocFamily::S3C2410:   return "S3C2410";
        case SocFamily::SA1110:    return "SA1110";
        case SocFamily::SA1100:    return "SA1100";
        case SocFamily::PXA25x:    return "PXA25x";
        case SocFamily::PXA27x:    return "PXA27x";
        case SocFamily::OMAP3530:  return "OMAP3530";
        case SocFamily::Poseidon:  return "Poseidon";
        case SocFamily::iMX31:     return "iMX31";
        case SocFamily::iMX32:     return "iMX32";
        case SocFamily::iMX51:     return "iMX51";
        case SocFamily::TegraAPX:  return "TegraAPX";
        case SocFamily::VR5500:    return "VR5500";
    }
    return "Unknown";
}

std::string BoardDetector::ModuleNames() const {
    std::string joined;
    for (const auto& rom : emu_.Get<RomParserService>().Loaded()) {
        for (const auto& xip : rom.xips) {
            for (const auto& m : xip.toc.modules) {
                joined += m.lpszFileName;
                joined += '\n';
            }
        }
    }
    return joined;
}

std::vector<uint8_t> BoardDetector::ReadKernelBlob() const {
    /* Scoping the fingerprint scan to the kernel module's own bytes
       (rather than the whole ROM) avoids false positives on short
       SoC names (e.g. "ODO", "OMAP35") that may appear in unrelated
       module content. */
    const auto bytes = emu_.Get<RomParserService>().ModuleBytesByName("nk.exe");
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

bool BoardDetector::RomContainsString(const char* needle) const {
    for (const auto& rom : emu_.Get<RomParserService>().Loaded()) {
        if (ContainsString(rom.raw.data(), rom.raw.size(), needle)) return true;
    }
    return false;
}

bool BoardDetector::SecContainsString(const char* needle) const {
    auto* sf = emu_.TryGet<SecFlash>();
    if (!sf || !sf->IsPresent()) return false;

    const size_t nlen = std::strlen(needle);
    if (nlen == 0) return false;

    /* The OS XIP images (carrying the OAL fingerprint) sit in the low flash, so
       a bounded windowed scan finds them without reading the whole 2 GB image.
       Windows overlap by the UTF-16 needle width so a match straddling a window
       boundary is still caught. */
    constexpr uint64_t kScanLimit = 64ull * 1024 * 1024;
    constexpr size_t   kWindow    = 1u * 1024 * 1024;
    const size_t       overlap    = nlen * 2;

    const uint64_t limit = std::min<uint64_t>(kScanLimit, sf->FlashSize());
    std::vector<uint8_t> buf(kWindow);
    uint64_t pos = 0;
    while (pos < limit) {
        const size_t want =
            static_cast<size_t>(std::min<uint64_t>(kWindow, limit - pos));
        const size_t got = sf->ReadFlash(pos, buf.data(), want);
        if (got == 0) break;
        if (ContainsString(buf.data(), got, needle)) return true;
        if (got < want || got <= overlap) break;
        pos += got - overlap;
    }
    return false;
}

bool BoardDetector::ContainsString(const uint8_t* data, size_t size,
                                   const char* needle) {
    const size_t nlen = std::strlen(needle);
    if (nlen == 0 || size < nlen) return false;

    /* ASCII match. */
    {
        const size_t end = size - nlen;
        for (size_t i = 0; i <= end; ++i) {
            if (std::memcmp(data + i, needle, nlen) == 0) return true;
        }
    }
    /* UTF-16 LE match: each ASCII byte becomes byte + 0x00. */
    {
        std::vector<uint8_t> wide(nlen * 2);
        for (size_t i = 0; i < nlen; ++i) {
            wide[i * 2]     = static_cast<uint8_t>(needle[i]);
            wide[i * 2 + 1] = 0;
        }
        if (size < wide.size()) return false;
        const size_t end = size - wide.size();
        for (size_t i = 0; i <= end; ++i) {
            if (std::memcmp(data + i, wide.data(), wide.size()) == 0) return true;
        }
    }
    return false;
}

bool BoardDetector::NameContains(const std::string& names, const char* needle) {
    return names.find(needle) != std::string::npos;
}
