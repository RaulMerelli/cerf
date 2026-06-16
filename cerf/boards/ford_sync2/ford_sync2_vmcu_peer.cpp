#include "ford_sync2_vmcu_peer.h"

#include "../../core/cerf_emulator.h"
#include "../../boards/board_detector.h"
#include "../../socs/imx51/imx51_uart2.h"
#include "../../state/state_stream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

bool FordSync2VmcuPeer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardDetector>();
    return bd && bd->GetBoard() == Board::FordSyncGen2;
}

void FordSync2VmcuPeer::OnReady() {
    uart_ = &emu_.Get<Imx51Uart2>();
    uart_->AttachEndpoint(this);
}

std::vector<uint8_t> FordSync2VmcuPeer::EncodeFrame(const uint8_t* payload,
                                                    std::size_t len) {
    /* 16-bit additive checksum over the payload (header+data), little-endian,
       followed by a 0 pad: IPCMP frames the checksum as a 3-byte segment
       [ck_lo][ck_hi][00] (sub_C0E23130 *(v22+24)=3 segments). */
    uint32_t sum = 0u;
    for (std::size_t i = 0; i < len; ++i) sum += payload[i];
    sum &= 0xFFFFu;

    std::vector<uint8_t> flat(payload, payload + len);
    flat.push_back(static_cast<uint8_t>(sum & 0xFFu));
    flat.push_back(static_cast<uint8_t>(sum >> 8));
    flat.push_back(0u);

    std::vector<uint8_t> out;
    out.push_back(kFlag);
    std::size_t cur = 0;
    const std::size_t kN = flat.size();
    int lit = 0, tz = 0;
    while (cur < kN || lit > 0) {
        if (lit > 0) {
            out.push_back(flat[cur++]);
            if (--lit == 0 && tz > 0) { cur += static_cast<std::size_t>(tz); tz = 0; }
            continue;
        }
        if (cur >= kN) break;
        int nz = 0, z = 0;
        for (std::size_t i = cur; i < kN; ++i) {
            if (flat[i] != 0u) { if (z) break; if (++nz >= 207) break; }
            else ++z;
        }
        if (nz == 0) {
            const uint8_t h = (z == 1) ? 0x01u
                            : (z == 2) ? 0xE0u
                            : static_cast<uint8_t>(0xD0u + (z < 15 ? z : 15));
            out.push_back(h);
            cur += static_cast<std::size_t>(z);
        } else if (nz < 0x1F && z > 1) {
            out.push_back(static_cast<uint8_t>(nz + 0xE0));
            tz = 2; lit = nz;
        } else if (nz < 0xCF) {
            out.push_back(static_cast<uint8_t>(nz + 1));
            tz = 1; lit = nz;
        } else {
            out.push_back(0xD0u);
            tz = 0; lit = nz;
        }
    }
    /* FLAG <rle> FLAG FLAG: the deframer (sub_C0E229B0) dispatches a buffer only
       on a FLAG FLAG double-delimiter; a single trailing FLAG leaves it pending. */
    out.push_back(kFlag);
    out.push_back(kFlag);
    return out;
}

std::vector<uint8_t> FordSync2VmcuPeer::BuildLinkFrame(uint8_t type, uint8_t tid,
                                                       uint16_t token) {
    /* ipc.dll LINK packet (Cid 0): header(2)=01 00, data(12)=[type][Tid]
       [token:2][config-CRC:4][pad:4]. */
    const uint8_t payload[14] = {
        0x01u, 0x00u,
        type, tid,
        static_cast<uint8_t>(token & 0xFFu), static_cast<uint8_t>(token >> 8),
        static_cast<uint8_t>(kConfigCrc & 0xFFu),
        static_cast<uint8_t>((kConfigCrc >> 8) & 0xFFu),
        static_cast<uint8_t>((kConfigCrc >> 16) & 0xFFu),
        static_cast<uint8_t>((kConfigCrc >> 24) & 0xFFu),
        0u, 0u, 0u, 0u,
    };
    return EncodeFrame(payload, sizeof(payload));
}

std::vector<uint8_t> FordSync2VmcuPeer::BuildAckFrame(uint8_t cid, uint8_t ack_seq) {
    /* ipc.dll transport ACK (SendAck sub_C093B890): a header-only IPCMP packet,
       no data. byte0 = Cid<<2 | 2 (ACK type bit1) -> head RX routes to the RX-ACK
       handler sub_C093BE18; byte1 = next-expected-seq << 1 (the seq the head
       validates against its outstanding TX window); bytes[2..3] = RX window. */
    const uint8_t pkt[4] = {
        static_cast<uint8_t>((cid << 2) | 0x02u),
        static_cast<uint8_t>(ack_seq << 1),
        static_cast<uint8_t>(kAckWindow & 0xFFu),
        static_cast<uint8_t>(kAckWindow >> 8),
    };
    return EncodeFrame(pkt, sizeof(pkt));
}

void FordSync2VmcuPeer::OnHeadMessage(uint8_t type) {
    if (type == kMsgSetup) {
        /* Reply with our LINK_SETUP once. The head re-sends LINK_SETUP until
           answered; an extra reply arriving after it reaches state 6 hits
           ipc.dll's RX SM (sub_C093C7EC case 6, msg 1) "Re-starting Link Setup"
           -> DOWN, so LINK UP never completes. */
        if (state_ == PeerState::Down) {
            const auto f = BuildLinkFrame(kMsgSetup, peer_tid_, kPeerToken);
            uart_->InjectRx(f.data(), f.size());
            state_ = PeerState::SetupSent;
        }
    } else if (type == kMsgResp) {
        const auto f = BuildLinkFrame(kMsgCpl, peer_tid_, 0u);
        uart_->InjectRx(f.data(), f.size());
        state_ = PeerState::CplSent;
    }
}

void FordSync2VmcuPeer::OnHeadData(uint8_t cid, uint8_t rx_seq) {
    /* ACK the next-expected sequence (ipc.dll SendAck advances ES past the
       received seq before sending: byte1 = (rx_seq+1) mod 128). */
    const uint8_t ack_seq = static_cast<uint8_t>((rx_seq + 1u) & 0x7Fu);
    const auto f = BuildAckFrame(cid, ack_seq);
    uart_->InjectRx(f.data(), f.size());
}

std::size_t FordSync2VmcuPeer::Deframe(const std::vector<uint8_t>& rle, uint8_t* dec,
                                       std::size_t cap) const {
    /* RLE headers per the deframer sub_C0E229B0: <0xD0 = (H-1) literals + 1
       zero; 0xD0 = 207 literals; 0xD3..0xDF = (H&0xF) zeros; 0xE0..0xFE =
       (H&0x1F) literals + 2 zeros. */
    std::size_t n = 0, i = 0;
    while (i < rle.size() && n < cap) {
        const uint8_t h = rle[i++];
        int lit = 0, zr = 0;
        if (h < 0xD0u)                    { lit = h - 1;     zr = 1; }
        else if (h == 0xD0u)              { lit = 207;       zr = 0; }
        else if (h >= 0xD3u && h < 0xE0u) { lit = 0;         zr = h & 0xFu; }
        else if (h >= 0xE0u && h < 0xFFu) { lit = h & 0x1Fu; zr = 2; }
        else return 0u;  /* 0xD1 / 0xD2 / 0xFF = malformed */
        for (int k = 0; k < lit && i < rle.size() && n < cap; ++k)
            dec[n++] = rle[i++];
        for (int k = 0; k < zr && n < cap; ++k) dec[n++] = 0u;
    }
    return n;
}

void FordSync2VmcuPeer::OnGuestTx(uint8_t byte) {
    if (byte == kFlag) {
        if (!tx_frame_.empty()) {
            uint8_t dec[16] = {0};
            const std::size_t n = Deframe(tx_frame_, dec, sizeof(dec));
            if (n >= 3 && dec[0] == 0x01u && dec[1] == 0x00u) {
                OnHeadMessage(dec[2]);  /* LINK packet (Cid 0): data[0] = msg type */
            } else if (n >= 2 && (dec[0] >> 2) > 3u && (dec[0] & 3u) == 0u) {
                /* reliable data on a data channel (Cid>3): ack to release the
                   head's TX resend. byte1 = seq<<1. */
                OnHeadData(static_cast<uint8_t>(dec[0] >> 2),
                           static_cast<uint8_t>(dec[1] >> 1));
            }
            tx_frame_.clear();
        }
        return;
    }
    if (tx_frame_.size() >= kMaxTxFrame) tx_frame_.clear();
    tx_frame_.push_back(byte);
}

void FordSync2VmcuPeer::SaveState(StateWriter& w) {
    w.Write<uint8_t>(static_cast<uint8_t>(state_));
    w.Write(peer_tid_);
}

void FordSync2VmcuPeer::RestoreState(StateReader& r) {
    uint8_t s = 0;
    r.Read(s);
    state_ = static_cast<PeerState>(s);
    r.Read(peer_tid_);
}

REGISTER_SERVICE(FordSync2VmcuPeer);
