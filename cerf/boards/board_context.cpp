#include "board_context.h"

#include "../core/cerf_emulator.h"
#include "../core/device_config.h"
#include "../core/log.h"

namespace {

constexpr BoardIdEntry kBoardIds[] = {
    {"devemu",            Board::Smdk2410DevEmu},
    {"odo",               Board::OdoArm720},
    {"omap_3530_evm",     Board::OmapEvm3530},
    {"ipaq_gen1",         Board::IpaqGen1},
    {"zune_30",           Board::ZuneKeel},
    {"falcon_4220",       Board::FalconPC3xx},
    {"jornada_720",       Board::Jornada720},
    {"jornada_820",       Board::Jornada820},
    {"simpad_sl4",        Board::SimpadSl4},
    {"nec_mobilepro_900", Board::NecMobilePro900},
    {"ford_sync_2",       Board::FordSyncGen2},
    {"siemens_p177",      Board::SiemensP177},
    {"smartbook_g138",    Board::SmartBookG138},
    {"nec_rockhopper",    Board::NecRockhopper},
    {"nec_mobilepro_700", Board::NecMobilePro700},
    {"philips_nino_300",  Board::PhilipsNino300},
};

}  /* namespace */

bool BoardContext::ShouldRegister() {
    return BoardFromId(emu_.Get<DeviceConfig>().board_id) == GetBoard();
}

void BoardContext::OnReady() {
    LOG(Board, "board: %s (SoC %s)\n", BoardName(), SocFamilyName(GetSoc()));
}

std::span<const BoardIdEntry> BoardContext::BoardIds() {
    return std::span<const BoardIdEntry>(kBoardIds);
}

Board BoardContext::BoardFromId(const std::string& id) {
    if (id.empty()) return Board::Unknown;
    for (const auto& e : kBoardIds) {
        if (id == e.id) return e.board;
    }
    return Board::Unknown;
}

const char* BoardContext::SocFamilyName(SocFamily f) {
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
        case SocFamily::VR4102:    return "VR4102";
        case SocFamily::PR31700:   return "PR31700";
    }
    return "Unknown";
}
