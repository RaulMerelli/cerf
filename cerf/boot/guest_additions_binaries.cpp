#include "guest_additions_binaries.h"

#include "../core/cerf_emulator.h"
#include "../core/string_utils.h"
#include "../cpu/arm_processor_config.h"

REGISTER_SERVICE(GuestAdditionsBinaries);

namespace {

std::string VariantDllName(const std::string& name, bool thumb_cpu) {
    if (!thumb_cpu) return name;
    const size_t dot = name.rfind('.');
    return (dot == std::string::npos)
        ? name + "_thumb"
        : name.substr(0, dot) + "_thumb" + name.substr(dot);
}

}  /* namespace */

std::string GuestAdditionsBinaries::StagedPath(const std::string& dll_name) {
    const bool thumb = emu_.Get<ArmProcessorConfig>().HasThumb();
    return GetCerfDir() + "ce_apps\\" + VariantDllName(dll_name, thumb);
}
