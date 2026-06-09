#include "pcmcia_card_catalog.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../compactflash/compactflash_menu.h"
#include "../hp_palmtop_vga/hp_palmtop_vga_card.h"
#include "../hp_palmtop_vga/hp_palmtop_vga_card_menu.h"
#include "../realtek_rtl8019/rtl8019.h"
#include "../serial_pccard/serial_pccard.h"
#include "../serial_pccard/serial_modem_card_menu.h"

namespace {

const char kIdNe2000[] = "ne2000";
const char kIdHpVga[]  = "hpvga";
const char kIdSerial[] = "serial";

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

    Entry hpvga;
    hpvga.id           = kIdHpVga;
    hpvga.display_name = HpPalmtopVgaCard::kDisplayName;
    hpvga.insert_submenu = [this](CardInserter inserter) {
        return emu_.Get<HpPalmtopVgaCardMenu>().BuildInsertMenu(std::move(inserter));
    };

    Entry serial;
    serial.id           = kIdSerial;
    serial.display_name = SerialPcCard::kDisplayName;
    serial.insert_submenu = [this](CardInserter inserter) {
        return emu_.Get<SerialModemCardMenu>().BuildInsertMenu(std::move(inserter));
    };

    entries_.push_back(std::move(ne2000));
    entries_.push_back(std::move(cf));
    entries_.push_back(std::move(hpvga));
    entries_.push_back(std::move(serial));
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
