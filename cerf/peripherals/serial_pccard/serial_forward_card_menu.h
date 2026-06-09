#pragma once

#include "../../core/service.h"
#include "../../host/host_widget.h"
#include "../pcmcia/pcmcia_card_catalog.h"

#include <string>
#include <vector>

/* Builds the "Serial Port Forwarder" insert submenu: one entry per host serial
   port, each inserting a serial PC card bridged to that real port, plus inline
   (disabled) guidance. */
class SerialForwardCardMenu : public Service {
public:
    using Service::Service;

    std::vector<WidgetMenuItem> BuildInsertMenu(
        PcmciaCardCatalog::CardInserter inserter);

private:
    /* Host serial ports from HKLM\HARDWARE\DEVICEMAP\SERIALCOMM (e.g. "COM3"). */
    std::vector<std::wstring> EnumerateHostComPorts() const;
};
