#pragma once

#include "siemens_mp377_smi_bridge_window.h"

namespace siemens_mp377 {

class SiemensMp377SmiBridgeC480 : public SiemensMp377SmiBridgeWindow {
public:
    using SiemensMp377SmiBridgeWindow::SiemensMp377SmiBridgeWindow;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

protected:
    bool IsC410ConsoleAlias() const override { return false; }
};

} // namespace siemens_mp377
