#include "../../core/service.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../state/state_stream.h"
#include "../board_context.h"
#include "../../socs/uart_endpoint.h"
#include "../../socs/imx6/imx6_uart2.h"

#include <cstdint>
#include <vector>
#include "ktp_mobile_id.h"

namespace {

class KtpMobileConnBox : public Service, public UartEndpoint {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && BoardId::IsKtpMobile(bd->GetBoardId());
    }
    // hmi_ktp400_mobile_v13 ConnBox.dll: BOX_Init @ 0xEF493334 opens COM2:.
    void OnReady() override { emu_.Get<Imx6Uart2>().AttachEndpoint(this); }

    void OnUartReset(ResetLineKind) override {
        prev_dle_ = false;
        in_frame_ = false;
        rx_payload_.clear();
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>("prev_dle", prev_dle_ ? 1u : 0u);
        w.Write<uint8_t>("in_frame", in_frame_ ? 1u : 0u);
        w.Write<uint32_t>("rx_payload_count", static_cast<uint32_t>(rx_payload_.size()));
        w.WriteBytes("rx_payload", rx_payload_.data(), rx_payload_.size());
    }

    void RestoreState(StateReader& r) override {
        uint8_t prev_dle = 0;
        uint8_t in_frame = 0;
        uint32_t payload_size = 0;
        r.Read("prev_dle", prev_dle);
        r.Read("in_frame", in_frame);
        r.Read("rx_payload_count", payload_size);
        if (payload_size > 7u) {
            r.Reject("ConnBox payload %u exceeds 7 bytes", payload_size);
        }
        prev_dle_ = prev_dle != 0u;
        in_frame_ = in_frame != 0u;
        rx_payload_.resize(payload_size);
        r.ReadBytes("rx_payload", rx_payload_.data(), rx_payload_.size());
    }

    // hmi_ktp400_mobile_v13 ConnBox.dll: sub_EF492A54 @ 0xEF492A54 sends and sub_EF4927D0 @ 0xEF4927D0 parses
    // DLE/STX + 7-byte DLE-escaped payload + DLE/ETX; hmi_ktp400_mobile_v17 ConnBox.dll confirms it in
    // sub_EF493D1C @ 0xEF493D1C and sub_EF493BC0 @ 0xEF493BC0.
    void OnGuestTx(uint8_t byte) override {
        if (!in_frame_) {
            if (!prev_dle_) {
                if (byte != 0x10u) {
                    emu_.Get<Fatal>().Die("KTP Mobile ConnBox expected DLE, got 0x%02X",
                                          static_cast<unsigned>(byte));
                }
                prev_dle_ = true;
                return;
            }
            prev_dle_ = false;
            if (byte != 0x02u) {
                emu_.Get<Fatal>().Die("KTP Mobile ConnBox expected STX, got 0x%02X",
                                      static_cast<unsigned>(byte));
            }
            in_frame_ = true;
            rx_payload_.clear();
            return;
        }

        if (prev_dle_) {
            prev_dle_ = false;
            if (byte == 0x10u) {
                AppendPayloadByte(0x10u);
                return;
            }
            if (byte == 0x03u) {
                in_frame_ = false;
                ReplyToMicroOmsFrame();
                return;
            }
            emu_.Get<Fatal>().Die("KTP Mobile ConnBox malformed DLE escape 0x%02X",
                                  static_cast<unsigned>(byte));
        }

        if (byte == 0x10u) {
            prev_dle_ = true;
            return;
        }
        AppendPayloadByte(byte);
    }

private:
    void AppendPayloadByte(uint8_t byte) {
        if (rx_payload_.size() >= 7u) {
            emu_.Get<Fatal>().Die("KTP Mobile ConnBox payload exceeds 7 bytes");
        }
        rx_payload_.push_back(byte);
    }

    // hmi_ktp400_mobile_v13, ConnBox.dll: unk_EF4940A4 used by sub_EF4931D0 @ VA 0xEF4931D0
    // is 4A 55 0F 00 00 00 00; sub_EF493234 @ VA 0xEF493234 accepts 4A AA F0 + version.
    void ReplyToMicroOmsFrame() {
        if (rx_payload_.size() != 7u || rx_payload_[0] != 0x4Au || rx_payload_[1] != 0x55u ||
            rx_payload_[2] != 0x0Fu || rx_payload_[3] != 0x00u || rx_payload_[4] != 0x00u ||
            rx_payload_[5] != 0x00u || rx_payload_[6] != 0x00u) {
            const uint8_t b0 = rx_payload_.size() > 0u ? rx_payload_[0] : 0u;
            const uint8_t b1 = rx_payload_.size() > 1u ? rx_payload_[1] : 0u;
            const uint8_t b2 = rx_payload_.size() > 2u ? rx_payload_[2] : 0u;
            emu_.Get<Fatal>().Die("KTP Mobile ConnBox unsupported frame size=%u prefix=%02X %02X %02X",
                                  static_cast<unsigned>(rx_payload_.size()), b0, b1, b2);
        }

        const std::vector<uint8_t> reply = {
            0x4Au, 0xAAu, 0xF0u,
            static_cast<uint8_t>(kConnBoxVersionStub >> 24u),
            static_cast<uint8_t>(kConnBoxVersionStub >> 16u),
            static_cast<uint8_t>(kConnBoxVersionStub >> 8u),
            static_cast<uint8_t>(kConnBoxVersionStub),
        };

        std::vector<uint8_t> wire;
        wire.reserve(reply.size() + 4u);
        wire.push_back(0x10u);
        wire.push_back(0x02u);
        for (uint8_t b : reply) {
            wire.push_back(b);
            if (b == 0x10u) wire.push_back(0x10u);
        }
        wire.push_back(0x10u);
        wire.push_back(0x03u);
        emu_.Get<Imx6Uart2>().InjectRx(wire.data(), wire.size());
    }

    static constexpr uint32_t kConnBoxVersionStub = 0u;

    bool prev_dle_ = false;
    bool in_frame_ = false;
    std::vector<uint8_t> rx_payload_;
};

}

REGISTER_SERVICE(KtpMobileConnBox);
