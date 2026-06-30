#include "boot_mode.h"

#include "rom_parser_service.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../boards/page_table_builder.h"

namespace {

class XipPlaceBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetRomPlacingMode()
            == RomPlacingMode::FlatContainer;
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
