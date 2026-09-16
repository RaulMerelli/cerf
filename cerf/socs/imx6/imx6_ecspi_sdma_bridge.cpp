#include "../freescale_sdma_soc_channel.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_ecspi_endpoint.h"

#include <cstring>

namespace {

class Imx6EcspiSdmaBridge final : public FreescaleSdmaSocChannel {
public:
    using FreescaleSdmaSocChannel::FreescaleSdmaSocChannel;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetSoc() == SocFamily::iMX6;
    }

    bool Handles(uint32_t channel, int event) const override {
        /* hmi_ktp400_mobile_v17 ecspi.dll @0x100033B0 and @0x10003404. */
        return (channel == 1u && event == 7) || (channel == 2u && event == 8);
    }

    void Complete(uint32_t channel, uint32_t mode, uint32_t buffer_pa) override {
        if (channel != 1u && channel != 2u) {
            emu_.Get<Fatal>().Die("Imx6EcspiSdmaBridge::Complete: channel %u is not an ECSPI3 "
                                  "SDMA channel (mode=0x%08X buffer_pa=0x%08X)",
                                  channel, mode, buffer_pa);
        }

        constexpr uint32_t kEcspi3Base = 0x02010000u;
        constexpr uint32_t kRxData = kEcspi3Base + 0x00u;
        constexpr uint32_t kTxData = kEcspi3Base + 0x04u;

        auto& memory = emu_.Get<EmulatedMemory>();
        auto& io = emu_.Get<PeripheralDispatcher>();
        const uint32_t bytes = mode & 0xFFFFu;

        if (auto* endpoint = emu_.TryGet<Imx6EcspiEndpoint>();
            endpoint && endpoint->EcspiBase() == kEcspi3Base) {
            if (channel == 2u)
                endpoint->StageDmaTransmit(buffer_pa, bytes);
            else
                endpoint->StageDmaReceive(buffer_pa, bytes);
            return;
        }

        if (channel == 1u) {
            for (uint32_t off = 0; off < bytes; off += 4u) {
                uint32_t value = 0xFFFFFFFFu;
                if (uint8_t* src = memory.TryTranslate(buffer_pa + off)) std::memcpy(&value, src, sizeof(value));
                io.WriteWord(kTxData, value);
            }
            return;
        }

        for (uint32_t off = 0; off < bytes; off += 4u) {
            const uint32_t value = io.ReadWord(kRxData);
            if (uint8_t* dst = memory.TryTranslateWrite(buffer_pa + off)) std::memcpy(dst, &value, sizeof(value));
        }
    }
};

} // namespace

REGISTER_SERVICE_AS(Imx6EcspiSdmaBridge, FreescaleSdmaSocChannel);
