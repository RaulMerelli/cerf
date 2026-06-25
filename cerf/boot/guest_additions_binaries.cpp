#include "guest_additions_binaries.h"

#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "../cpu/arm_processor_config.h"
#include "../boards/board_detector.h"

REGISTER_SERVICE(GuestAdditionsBinaries);

namespace {
constexpr const char* kBodyDll = "cerf_guest.dll";
constexpr const char* kStubDll = "cerf_guest_stub.dll";
}  /* namespace */

/* ce_apps build-output arch subdir for the current guest CPU. MIPS guests have
   no ArmProcessorConfig, so the arch is decided before that service is touched;
   Thumb-capable ARM cores get the interworking build, no-Thumb cores pure ARM. */
std::string GuestAdditionsBinaries::ArchDir() {
    if (emu_.Get<BoardDetector>().GetCpuArch() == CpuArch::Mips)
        return "mips";
    return emu_.Get<ArmProcessorConfig>().HasThumb() ? "arm_thumb" : "arm";
}

std::string GuestAdditionsBinaries::BodyPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kBodyDll;
}

std::string GuestAdditionsBinaries::StubPath() {
    return GetCerfDir() + "ce_apps\\" + ArchDir() + "\\" + kStubDll;
}
