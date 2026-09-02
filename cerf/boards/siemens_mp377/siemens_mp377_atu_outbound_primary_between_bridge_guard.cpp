#include "mp377_board_io_window.h"

using namespace mp377_board_io_detail;

namespace {

class SiemensMp377AtuOutboundPrimaryBetweenBridgeGuard : public SiemensMp377AtuOutboundPrimaryGuard {
public:
    using SiemensMp377AtuOutboundPrimaryGuard::SiemensMp377AtuOutboundPrimaryGuard;
    uint32_t MmioBase() const override { return SmiBridgeEnd(Mp377SmiBridgeWindowId::C410); }
    uint32_t MmioSize() const override { return SmiBridgeBase(Mp377SmiBridgeWindowId::C480) - MmioBase(); }

};

} // namespace

REGISTER_SERVICE(SiemensMp377AtuOutboundPrimaryBetweenBridgeGuard);
