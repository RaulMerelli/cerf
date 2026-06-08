#include "pcmcia_card_catalog.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../compactflash/compactflash_menu.h"
#include "../realtek_rtl8019/rtl8019.h"

namespace {

const char kIdNe2000[] = "ne2000";

}  /* namespace */

void PcmciaCardCatalog::OnReady() {
    Entry ne2000;
    ne2000.id           = kIdNe2000;
    ne2000.display_name = Rtl8019::kDisplayName;

    Entry cf;
    cf.id           = "cf";
    cf.display_name = L"Compact Flash";
    cf.insert_submenu = [this](CardInserter inserter) {
        return emu_.Get<CompactFlashMenu>().BuildInsertMenu(std::move(inserter));
    };

    entries_.push_back(std::move(ne2000));
    entries_.push_back(std::move(cf));
}

std::unique_ptr<PcmciaCard> PcmciaCardCatalog::Create(const std::string& id) {
    if (id == kIdNe2000) {
        return std::make_unique<Rtl8019>(emu_);
    }
    LOG(Caution, "PcmciaCardCatalog::Create: unknown card id '%s'\n",
        id.c_str());
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

REGISTER_SERVICE(PcmciaCardCatalog);
