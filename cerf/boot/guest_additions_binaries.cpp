#include "guest_additions_binaries.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../core/string_utils.h"
#include "../cpu/arm_processor_config.h"
#include "../boards/board_context.h"
#include "../peripherals/cerf_virt/cerf_virt_addr_map.h"
#include "rom_parser_service.h"

REGISTER_SERVICE(GuestAdditionsBinaries);

namespace {
constexpr const char* kBodyDll = "cerf_guest.dll";
constexpr const char* kStubDll = "cerf_guest_stub.dll";
constexpr uint16_t kImageFileMachineMipsFpu = 0x366;
}  /* namespace */

std::string GuestAdditionsBinaries::ArchDir() {
    if (emu_.Get<BoardContext>().GetCpuArch() == CpuArch::Mips) {
        auto& parser = emu_.Get<RomParserService>();
        const ParsedRom& rom = parser.Primary();
        const uint16_t cpu = rom.xips.empty() ? 0 : rom.xips[0].toc.romhdr.usCPUType;
        if (cpu == kImageFileMachineMipsFpu) return "mips4";
        uint16_t maj = 0, min = 0;
        parser.KernelSubsystemVersion(maj, min);
        return maj <= 2 ? "mips2_ce2" : "mips2";
    }
    return emu_.Get<ArmProcessorConfig>().HasThumb() ? "arm_thumb" : "arm";
}

std::string GuestAdditionsBinaries::BodyPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kBodyDll;
}

std::string GuestAdditionsBinaries::StubPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kStubDll;
}

void GuestAdditionsBinaries::StampWindowBase(std::vector<uint8_t>& image) {
    const uint32_t base  = emu_.Get<BoardContext>().GuestAdditionsWindowBase();
    const uint32_t magic = CerfVirt::kBaseMagic;
    const uint8_t needle[4] = {
        uint8_t(magic & 0xFFu), uint8_t((magic >> 8) & 0xFFu),
        uint8_t((magic >> 16) & 0xFFu), uint8_t((magic >> 24) & 0xFFu) };

    size_t hits = 0, at = 0;
    for (size_t i = 0; i + 4u <= image.size(); ++i) {
        if (image[i] == needle[0] && image[i + 1] == needle[1] &&
            image[i + 2] == needle[2] && image[i + 3] == needle[3]) {
            ++hits;
            at = i;
        }
    }
    if (hits != 1) {
        LOG(Caution, "GuestAdditions: base sentinel 0x%08X found %zu time(s) in the "
                "guest image (expected exactly 1) - magic collided or g_CerfVirtBase "
                "is absent\n", magic, hits);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    image[at + 0] = uint8_t(base);
    image[at + 1] = uint8_t(base >> 8);
    image[at + 2] = uint8_t(base >> 16);
    image[at + 3] = uint8_t(base >> 24);
    LOG(GuestAdditions, "base sentinel stamped -> 0x%08X at image offset 0x%zX\n",
        base, at);
}
