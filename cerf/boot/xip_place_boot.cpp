#include "boot_mode.h"

#include "rom_parser_service.h"
#include "sec_flash.h"

#include "../boards/board_detector.h"
#include "../core/cerf_emulator.h"
#include "../boards/page_table_builder.h"

namespace {

class XipPlaceBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        if (emu_.Get<BoardDetector>().GetBoard() == Board::Unknown) return false;
        /* A `.sec` NAND image boots via Imx51NandBootloaderBoot instead. */
        if (auto* sf = emu_.TryGet<SecFlash>(); sf && sf->IsPresent()) return false;
        return true;
    }

    uint32_t ColdEntryPa() override {
        auto& rom = emu_.Get<RomParserService>();
        return emu_.Get<PageTableBuilder>().VaToPa(rom.EntryVa());
    }

    uint32_t ColdStackPa() override {
        return emu_.Get<PageTableBuilder>().InitStackTopPa();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(XipPlaceBoot, BootMode);
