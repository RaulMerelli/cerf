#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"
#include "../pcmcia/pcmcia_card_catalog.h"

#include <vector>

/* Builds the "Serial / Modem PC Card" insert submenu: an Insert action plus
   inline (disabled) guidance for dialing out through the modem the card
   presents, so the steps are visible on menu open without a dialog. */
class SerialModemCardMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(
        PcmciaCardCatalog::CardInserter inserter);
};
