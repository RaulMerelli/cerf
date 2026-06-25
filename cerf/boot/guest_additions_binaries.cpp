#include "guest_additions_binaries.h"

#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "../cpu/arm_processor_config.h"
#include "../boards/board_detector.h"
#include "rom_parser_service.h"

REGISTER_SERVICE(GuestAdditionsBinaries);

namespace {
constexpr const char* kBodyDll = "cerf_guest.dll";
constexpr const char* kStubDll = "cerf_guest_stub.dll";
constexpr uint16_t kImageFileMachineMipsFpu = 0x366;
}  /* namespace */

std::string GuestAdditionsBinaries::ArchDir() {
    if (emu_.Get<BoardDetector>().GetCpuArch() == CpuArch::Mips) {
        const ParsedRom& rom = emu_.Get<RomParserService>().Primary();
        const uint16_t cpu = rom.xips.empty() ? 0 : rom.xips[0].toc.romhdr.usCPUType;
        return cpu == kImageFileMachineMipsFpu ? "mips4" : "mips2";
    }
    return emu_.Get<ArmProcessorConfig>().HasThumb() ? "arm_thumb" : "arm";
}

std::string GuestAdditionsBinaries::BodyPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kBodyDll;
}

std::string GuestAdditionsBinaries::StubPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kStubDll;
}
