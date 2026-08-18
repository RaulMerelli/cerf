#include "pcmcia_auto_insert.h"

#include "../../boot/rom_parser_service.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "pcmcia_card_catalog.h"
#include "pcmcia_slot.h"

#include <cstdint>

REGISTER_SERVICE(PcmciaAutoInsert);

void PcmciaAutoInsert::InsertDefaultNetworkCard(PcmciaSlot& slot) {
    auto* rom = emu_.TryGet<RomParserService>();
    uint16_t major = 0, minor = 0;
    if (!rom || !rom->KernelSubsystemVersion(major, minor)) {
        LOG(Net, "PcmciaAutoInsert: guest kernel version unavailable; "
                 "leaving '%ls' empty\n", slot.WidgetName().c_str());
        return;
    }
    if (major < 4) {
        LOG(Net, "PcmciaAutoInsert: guest kernel is CE %u.%u; leaving "
                 "'%ls' empty\n", major, minor, slot.WidgetName().c_str());
        return;
    }

    slot.InsertCard(emu_.Get<PcmciaCardCatalog>().Create("ne2000"));
}
