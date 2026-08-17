#include "../../core/service.h"
#include "../../core/cerf_emulator.h"
#include "../board_context.h"
#include "../../socs/uart_endpoint.h"
#include "../../socs/imx6/imx6_uart2.h"

#include <cstdint>
#include <vector>

namespace {

/* KTP400 external MicroOMS/ConnBox companion on UART2 (board hardware on the
   UART2 pins). ConnBox.dll frames packets as 10 02 <7-byte payload, DLE escaped
   as 10 10> 10 03; DoPingSend() sends 4A 55 0F.. and accepts 4A AA F0 + u32. */
class HmiKtp400MobileConnBox : public Service, public UartEndpoint {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::HmiKtp400Mobile;
    }
    void OnReady() override {
        uart_ = &emu_.Get<Imx6Uart2>();
        uart_->AttachEndpoint(this);
    }

    void OnControlWrite(uint32_t reg_off, uint32_t value) override {
        /* ConnBox/readBoxID opens UART2 and waits for two bytes: BoxID, then a
           byte whose top bits encode BoxType (0xa0..0xbf = BoxType 3, the
           extended protocol). Inject once the driver enables the receiver
           (UCR2 = 0x84, RX-enable bit 0x20) so the bytes survive the port open. */
        if (box_id_injected_ || reg_off != 0x84u || (value & 0x20u) == 0u)
            return;
        const uint8_t id[] = { 0x00u, 0xA0u };
        uart_->InjectRx(id, sizeof(id));
        box_id_injected_ = true;
    }

    void OnGuestTx(uint8_t byte) override {
        if (!in_frame_) {
            if (prev_dle_ && byte == 0x02u) {
                in_frame_ = true;
                rx_payload_.clear();
                prev_dle_ = false;
                return;
            }
            prev_dle_ = (byte == 0x10u);
            return;
        }

        if (prev_dle_) {
            prev_dle_ = false;
            if (byte == 0x10u) {
                rx_payload_.push_back(0x10u);
                return;
            }
            if (byte == 0x03u) {
                in_frame_ = false;
                ReplyToMicroOmsFrame();
                return;
            }
            /* Broken escape: resync on the next DLE/STX. */
            in_frame_ = false;
            rx_payload_.clear();
            prev_dle_ = (byte == 0x10u);
            return;
        }

        if (byte == 0x10u) {
            if (rx_payload_.size() >= 7u) {
                /* ConnBox.dll writes all but the final ETX through WriteFile()
                   then pokes UTXD directly; the WriteFile path delivers the
                   trailing DLE here while the direct last-byte store can bypass
                   the endpoint, so close the fixed 7-byte frame at that DLE. */
                in_frame_ = false;
                prev_dle_ = false;
                ReplyToMicroOmsFrame();
                return;
            }
            prev_dle_ = true;
            return;
        }
        if (rx_payload_.size() < 64u)
            rx_payload_.push_back(byte);
    }

private:
    void ReplyToMicroOmsFrame() {
        if (rx_payload_.empty())
            return;

        std::vector<uint8_t> reply;
        if (rx_payload_.size() >= 3u && rx_payload_[0] == 0x4Au &&
            rx_payload_[1] == 0x55u && rx_payload_[2] == 0x0Fu) {
            /* DoPingSend(): 4A 55 0F request, 4A AA F0 + firmware version. The
               driver only requires the magic and caches/logs the u32. */
            reply = { 0x4Au, 0xAAu, 0xF0u, 0x00u, 0x00u, 0x00u, 0x01u };
        } else {
            /* Other exchange-information commands are 7-byte request/response
               records; ConnBox's common path validates response[0]==request[0],
               so echo the command byte with a zero payload. */
            reply.assign(7u, 0u);
            reply[0] = rx_payload_[0];
            for (size_t i = 1; i < rx_payload_.size() && i < reply.size(); ++i)
                reply[i] = rx_payload_[i];
        }

        std::vector<uint8_t> wire;
        wire.reserve(reply.size() + 4u);
        wire.push_back(0x10u);
        wire.push_back(0x02u);
        for (uint8_t b : reply) {
            wire.push_back(b);
            if (b == 0x10u)
                wire.push_back(0x10u);
        }
        wire.push_back(0x10u);
        wire.push_back(0x03u);
        uart_->InjectRx(wire.data(), wire.size());
    }

    Imx6Uart2* uart_ = nullptr;
    bool box_id_injected_ = false;
    bool prev_dle_ = false;
    bool in_frame_ = false;
    std::vector<uint8_t> rx_payload_;
};

}  // namespace

REGISTER_SERVICE(HmiKtp400MobileConnBox);

