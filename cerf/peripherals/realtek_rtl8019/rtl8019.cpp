#include "rtl8019.h"

#include "../pcmcia/pcmcia_slot.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../net/network_backend.h"
#include "../../state/state_stream.h"

#include <cstdio>
#include <cstring>

namespace {

const uint8_t kCisData[] = {
    0x01, 3, 0xDC, 0x00, 0xFF,
    0x17, 3, 0x49, 0x00, 0xFF,
    0x21, 2, 0x06, 0x03,
    0x15, 27, 0x04, 0x01, 0x50, 0x43, 0x4D, 0x43, 0x49, 0x41, 0x00,
              0x45, 0x74, 0x68, 0x65, 0x72, 0x6E, 0x65, 0x74, 0x20,
              0x43, 0x61, 0x72, 0x64, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x13, 3, 0x43, 0x49, 0x53,
    0x1A, 5, 0x01, 0x24, 0xF8, 0x03, 0x03,
    0x1B, 17, 0xE0, 0x81, 0x1D, 0x3F, 0x55, 0x4D, 0x5D, 0x06, 0x86,
              0x46, 0x26, 0xFC, 0x24, 0x65, 0x30, 0xFF, 0xFF,
    0x1B, 7, 0x20, 0x08, 0xCA, 0x60, 0x00, 0x03, 0x1F,
    0x1B, 7, 0x21, 0x08, 0xCA, 0x60, 0x20, 0x03, 0x1F,
    0x1B, 7, 0x22, 0x08, 0xCA, 0x60, 0x40, 0x03, 0x1F,
    0x1B, 7, 0x23, 0x08, 0xCA, 0x60, 0x60, 0x03, 0x1F,
    0x20, 4, 0x01, 0x8A, 0x00, 0x01,
    0x14, 0,
    0xFF, 0,
};
constexpr std::size_t kCisSize = sizeof(kCisData);

/* PC Card Standard Vol. 2 Electrical, 4.15 note 1: the configuration
   registers sit at the base the CIS Configuration Tuple TPCC_RADR
   declares (0x3F8 in the 0x1A tuple above); COR at base + 0, CCSR at
   base + 2 per the 4.15 register table. */
constexpr uint32_t kCorOffset  = 0x3F8;
constexpr uint32_t kCcsrOffset = 0x3FA;

/* PC Card Standard Vol. 2 Electrical, 4.15.1 Table 4-29. */
constexpr uint8_t kCorSreset  = 0x80;
constexpr uint8_t kCorLevIreq = 0x40;
constexpr uint8_t kCorIndex   = 0x3F;

/* PC Card Standard Vol. 2 Electrical, 4.15.2 Table 4-30. */
constexpr uint8_t kCcsrIntrAck = 0x01;
constexpr uint8_t kCcsrIntr    = 0x02;
constexpr uint8_t kCcsrPwrDwn  = 0x04;
constexpr uint8_t kCcsrIoIs8   = 0x20;
constexpr uint8_t kCcsrChanged = 0x80;

/* PC Card Standard Vol. 2 Electrical, 4.15.1: the low six COR bits are
   the Function Configuration Index. The CIS CFTABLE entries 0x20-0x23
   each decode 0x20 bytes of I/O at 0x300 + (index - 0x20) * 0x20. */
constexpr uint8_t  kCorIndexFirst = 0x20;
constexpr uint8_t  kCorIndexLast  = 0x23;
constexpr uint32_t kIoBlockBase   = 0x300;
constexpr uint32_t kIoBlockSize   = 0x20;

}

Rtl8019::Rtl8019(CerfEmulator& emu)
    : PcmciaCard(emu), nic_(*this, card_rom_, card_ram_),
      receiver_(nic_, card_ram_) {
    guest_mac_ = emu_.Get<NetworkBackend>().GuestMacAddress();
    /* QEMU hw/net/ne2000.c:124-137 ne2000_reset PROM layout;
       linux-2.6.25 drivers/net/pcmcia/pcnet_cs.c:389-392 get_prom()
       binds iff prom[28] == 0x57 && prom[30] == 0x57. */
    std::array<uint8_t, 16> prom{};
    for (std::size_t i = 0; i < kMacLen; ++i) {
        prom[i] = guest_mac_[i];
    }
    prom[14] = 0x57;
    prom[15] = 0x57;
    for (std::size_t i = 0; i < prom.size(); ++i) {
        card_rom_[2 * i]     = prom[i];
        card_rom_[2 * i + 1] = prom[i];
    }

    std::lock_guard<std::mutex> lk(state_mutex_);
    nic_.ResetLocked();
}

Rtl8019::~Rtl8019() { DetachRx(); }

void Rtl8019::DetachRx() {
    if (!rx_installed_) return;
    emu_.Get<NetworkBackend>().SetReceiveCallback(nullptr);
    rx_installed_ = false;
}

void Rtl8019::OnShutdown() {
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        const bool was_driving = powered_ && irq_line_;
        powered_  = false;
        irq_line_ = false;
        if (was_driving) slot_->ClearIrq();
    }
    DetachRx();
}

void Rtl8019::OnInserted() {
    emu_.Get<NetworkBackend>().SetReceiveCallback(
        [this](const uint8_t* frame, std::size_t len) {
            OnRxFrame(frame, len);
        });
    rx_installed_ = true;
    LOG(Net, "[NE2000] inserted: MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
        guest_mac_[0], guest_mac_[1], guest_mac_[2],
        guest_mac_[3], guest_mac_[4], guest_mac_[5]);
}

void Rtl8019::PowerOn() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    powered_ = true;
    card_ram_.fill(0);
    PowerUpLocked();
    LOG(Net, "[NE2000] power-on\n");
}

void Rtl8019::PowerUpLocked() {
    ccsr_ = 0u;
    nic_.PowerUpLocked();
}

void Rtl8019::PowerOff() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const bool was_driving = powered_ && irq_line_;
    powered_  = false;
    irq_line_ = false;
    if (was_driving) slot_->ClearIrq();
    /* PC Card Standard Vol. 2 Electrical, 4.3.2: the Memory Only
       interface is the default selected after the application of VCC. */
    cor_ = 0u;
    LOG(Net, "[NE2000] power-off\n");
}

void Rtl8019::SetIrqLineLocked(bool level) {
    if (!powered_) return;
    if (level == irq_line_) return;
    irq_line_ = level;
    if (level) slot_->RaiseIrq();
    else       slot_->ClearIrq();
}

void Rtl8019::OnRxFrame(const uint8_t* frame, std::size_t len) {
    /* linux-2.6.25 drivers/net/lib8390.c:737 - the 8390 driver rejects
       ring packets outside 60..1518 bytes as "bogus packet size". */
    if (len > 1518u) {
        LOG(Caution, "[NE2000] RX frame len=%u exceeds 1518; halting\n",
            static_cast<unsigned>(len));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint8_t padded[60];
    if (len < sizeof(padded)) {
        std::memcpy(padded, frame, len);
        std::memset(padded + len, 0, sizeof(padded) - len);
        frame = padded;
        len = sizeof(padded);
    }

    std::lock_guard<std::mutex> lk(state_mutex_);
    /* PC Card Standard Vol. 2 Electrical, 4.15.2 CCSR.PwrDwn: "the
       function shall enter a power-down state ... While this field is
       one (1), the host shall not access the function". */
    if (ccsr_ & kCcsrPwrDwn) return;
    if (receiver_.OnFrameLocked(frame, len)) {
        slot_->MarkRx();
    }
}

uint8_t Rtl8019::ReadAttribute8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset == kCorOffset) {
        return cor_;
    }
    if (offset == kCcsrOffset) {
        const uint8_t intr = nic_.IrqPendingLocked() ? kCcsrIntr : 0u;
        return static_cast<uint8_t>((ccsr_ & ~kCcsrIntr) | intr);
    }
    if (offset < kCisSize * 2u) {
        /* PC Card Standard Vol. 2 Electrical, Attribute Memory Read
           function: "only even byte data is valid". */
        return kCisData[offset / 2u];
    }
    return kBusFloat8;
}

void Rtl8019::WriteAttribute8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset == kCorOffset) {
        LOG(Net, "[NE2000] COR = 0x%02X (config index 0x%02X)\n",
            value, value & 0x3Fu);
        /* PC Card Standard Vol. 2 Electrical, 4.12.2: on SRESET "a card
           shall return to the power-up state"; 4.15.1: the index "shall
           be reset to zero (0) by the PC Card when the host sets the
           SRESET field to one (1)". */
        if (value & kCorSreset) {
            PowerUpLocked();
            cor_ = kCorSreset;
            return;
        }
        /* PC Card Standard Vol. 2 Electrical, 4.15.1 Table 4-29:
           LevIREQ selects Level vs Pulse Mode Interrupt; the card
           models the level form only. */
        if ((value & kCorIndex) != 0u && !(value & kCorLevIreq)) {
            LOG(Caution, "[NE2000] COR = 0x%02X selects pulse-mode "
                    "IREQ - unimplemented; halting\n", value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        cor_ = value;
        return;
    }
    if (offset == kCcsrOffset) {
        /* PC Card Standard Vol. 2 Electrical, 4.15.2 Table 4-30 field
           text: IntrAck ignored by single-function cards, Changed/Intr
           R/O, IOIs8 and PwrDwn stored. */
        if (value & static_cast<uint8_t>(
                ~(kCcsrIoIs8 | kCcsrPwrDwn | kCcsrIntrAck | kCcsrIntr |
                  kCcsrChanged))) {
            LOG(Caution, "[NE2000] CCSR = 0x%02X - SigChg/Audio/RFU "
                    "unimplemented; halting\n", value);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        ccsr_ = value & static_cast<uint8_t>(kCcsrIoIs8 | kCcsrPwrDwn);
        return;
    }
    LOG(Caution, "[NE2000] write attribute offset 0x%X = 0x%02X - "
            "unsupported register; halting\n", offset, value);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

/* linux-2.6.25 drivers/net/pcmcia/pcnet_cs.c:1481-1516
   setup_shmem_window(): packet RAM as a common-memory window at
   start_pg << 8. */
uint8_t Rtl8019::ReadCommon8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= Dp8390::kRamBase &&
        offset < Dp8390::kRamBase + Dp8390::kRamSize) {
        return card_ram_[offset - Dp8390::kRamBase];
    }
    LOG(Caution, "[NE2000] read8 common offset 0x%X outside the packet "
            "RAM window; halting\n", offset);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint16_t Rtl8019::ReadCommon16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const uint32_t base = offset & ~1u;
    if (base >= Dp8390::kRamBase &&
        base + 1u < Dp8390::kRamBase + Dp8390::kRamSize) {
        const uint32_t off = base - Dp8390::kRamBase;
        return static_cast<uint16_t>(card_ram_[off]) |
               (static_cast<uint16_t>(card_ram_[off + 1]) << 8);
    }
    LOG(Caution, "[NE2000] read16 common offset 0x%X outside the packet "
            "RAM window; halting\n", offset);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void Rtl8019::WriteCommon8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (offset >= Dp8390::kRamBase &&
        offset < Dp8390::kRamBase + Dp8390::kRamSize) {
        card_ram_[offset - Dp8390::kRamBase] = value;
        return;
    }
    LOG(Caution, "[NE2000] write8 common offset 0x%X = 0x%02X outside "
            "the packet RAM window; halting\n", offset, value);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void Rtl8019::WriteCommon16(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const uint32_t base = offset & ~1u;
    if (base >= Dp8390::kRamBase &&
        base + 1u < Dp8390::kRamBase + Dp8390::kRamSize) {
        const uint32_t off = base - Dp8390::kRamBase;
        card_ram_[off]     = static_cast<uint8_t>(value & 0xFFu);
        card_ram_[off + 1] = static_cast<uint8_t>(value >> 8);
        return;
    }
    LOG(Caution, "[NE2000] write16 common offset 0x%X = 0x%04X outside "
            "the packet RAM window; halting\n", offset, value);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

bool Rtl8019::IoIgnoredLocked() const {
    /* PC Card Standard Vol. 2 Electrical, 4.3.2: the Memory Only
       interface is the default at insertion and after VCC/RESET; the
       function decodes no I/O until a configuration index is set. */
    return (cor_ & kCorIndex) == 0u;
}

bool Rtl8019::MapCardIoLocked(uint32_t card_io, uint32_t* reg) const {
    const uint8_t index = cor_ & kCorIndex;
    if (index < kCorIndexFirst || index > kCorIndexLast) return false;
    const uint32_t base =
        kIoBlockBase + (uint32_t)(index - kCorIndexFirst) * kIoBlockSize;
    if (card_io < base || card_io >= base + kIoBlockSize) return false;
    *reg = card_io - base;
    return true;
}

uint8_t Rtl8019::ReadIo8(uint32_t card_io) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (IoIgnoredLocked()) return kBusFloat8;
    uint32_t offset;
    if (!MapCardIoLocked(card_io, &offset)) {
        LOG(Caution, "[NE2000] read8 io 0x%X outside configured block "
                "(COR=0x%02X); halting\n", card_io, cor_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return nic_.IoRead8Locked(offset);
}

uint16_t Rtl8019::ReadIo16(uint32_t card_io) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (IoIgnoredLocked()) return kBusFloat16;
    uint32_t offset;
    if (!MapCardIoLocked(card_io, &offset)) {
        LOG(Caution, "[NE2000] read16 io 0x%X outside configured block "
                "(COR=0x%02X); halting\n", card_io, cor_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return nic_.IoRead16Locked(offset);
}

void Rtl8019::WriteIo8(uint32_t card_io, uint8_t value) {
    std::vector<uint8_t> tx_pending;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (IoIgnoredLocked()) return;
        uint32_t offset;
        if (!MapCardIoLocked(card_io, &offset)) {
            LOG(Caution, "[NE2000] write8 io 0x%X = 0x%02X outside "
                    "configured block (COR=0x%02X); halting\n",
                    card_io, value, cor_);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        nic_.IoWrite8Locked(offset, value, tx_pending);
    }
    if (!tx_pending.empty()) {
        emu_.Get<NetworkBackend>().SendFrame(tx_pending.data(),
                                             tx_pending.size());
        slot_->MarkTx();
        std::lock_guard<std::mutex> lk(state_mutex_);
        nic_.CompleteTxLocked();
    }
}

void Rtl8019::WriteIo16(uint32_t card_io, uint16_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (IoIgnoredLocked()) return;
    uint32_t offset;
    if (!MapCardIoLocked(card_io, &offset)) {
        LOG(Caution, "[NE2000] write16 io 0x%X = 0x%04X outside "
                "configured block (COR=0x%02X); halting\n",
                card_io, value, cor_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    nic_.IoWrite16Locked(offset, value);
}

std::wstring Rtl8019::TooltipDetail() const {
    wchar_t buf[64];
    swprintf_s(buf, L"Ethernet  %02X:%02X:%02X:%02X:%02X:%02X",
               guest_mac_[0], guest_mac_[1], guest_mac_[2],
               guest_mac_[3], guest_mac_[4], guest_mac_[5]);
    return buf;
}

std::vector<WidgetMenuItem> Rtl8019::BuildCardMenu() {
    wchar_t buf[64];
    swprintf_s(buf, L"MAC  %02X:%02X:%02X:%02X:%02X:%02X",
               guest_mac_[0], guest_mac_[1], guest_mac_[2],
               guest_mac_[3], guest_mac_[4], guest_mac_[5]);
    WidgetMenuItem mac;
    mac.label   = buf;
    mac.enabled = false;
    return { std::move(mac) };
}

void Rtl8019::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.WriteBytes(guest_mac_.data(), guest_mac_.size());
    nic_.SaveState(w);
    w.Write(cor_); w.Write(ccsr_);
    w.WriteBytes(card_rom_.data(), card_rom_.size());
    w.WriteBytes(card_ram_.data(), card_ram_.size());
}

void Rtl8019::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    r.ReadBytes(guest_mac_.data(), guest_mac_.size());
    nic_.RestoreState(r);
    r.Read(cor_); r.Read(ccsr_);
    r.ReadBytes(card_rom_.data(), card_rom_.size());
    r.ReadBytes(card_ram_.data(), card_ram_.size());
}

void Rtl8019::PostRestore() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    irq_line_ = false;
    nic_.RecomputeIrqLocked();
}
