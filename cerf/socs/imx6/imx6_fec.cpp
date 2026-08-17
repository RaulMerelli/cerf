#include "imx6_gic.h"

#include "imx6_fec_legacy_ring.h"

#include "../../boards/board_context.h"
#include "../../cpu/emulated_memory.h"
#include "../../core/cerf_emulator.h"
#include "../../net/network_backend.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

namespace {

/* i.MX6 Fast Ethernet Controller / ENET.
   Linux imx6qdl.dtsi names this block ethernet@02188000 with a 0x4000-byte
   register window and GIC SPI 118.  The register layout follows the classic
   Freescale/NXP FEC block also implemented by QEMU's fsl_imx25/fec model:
   EIR/EIMR/RDAR/TDAR/ECR at 0x004/0x008/0x010/0x014/0x024, MII management at
   0x040/0x044, MAC address registers at 0x0E4/0x0E8, FIFO/control at 0x180+. */
class Imx6Fec final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }

    void OnReady() override {
        guest_mac_ = emu_.Get<NetworkBackend>().GuestMacAddress();
        palr_ = (uint32_t(guest_mac_[0]) << 24) |
                (uint32_t(guest_mac_[1]) << 16) |
                (uint32_t(guest_mac_[2]) <<  8) |
                 uint32_t(guest_mac_[3]);
        paur_ = (uint32_t(guest_mac_[4]) << 24) |
                (uint32_t(guest_mac_[5]) << 16) | 0x00008808u;
        emu_.Get<NetworkBackend>().SetReceiveCallback(
            [this](const uint8_t* frame, std::size_t len) {
                OnHostFrame(frame, len);
            });
        rx_installed_ = true;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    void OnShutdown() override {
        if (rx_installed_) {
            emu_.Get<NetworkBackend>().SetReceiveCallback(nullptr);
            rx_installed_ = false;
        }
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
    }
    uint32_t ReadWord(uint32_t addr) override { return ReadReg(addr - kBase); }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift   = (addr & 3u) * 8u;
        const uint32_t mask    = 0xFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | (static_cast<uint32_t>(value) << shift));
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift   = (addr & 2u) * 8u;
        const uint32_t mask    = 0xFFFFu << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | (static_cast<uint32_t>(value) << shift));
    }
    void WriteWord(uint32_t addr, uint32_t value) override { WriteReg(addr - kBase, value); }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.Write(eir_); w.Write(eimr_); w.Write(ecr_);
        w.Write(rcr_); w.Write(tcr_); w.Write(mmfr_); w.Write(mscr_);
        w.Write(mibc_); w.Write(iaur_); w.Write(ialr_); w.Write(gaur_); w.Write(galr_);
        w.Write(palr_); w.Write(paur_); w.Write(opd_);
        w.Write(tfwr_); w.Write(frbr_); w.Write(frsr_); w.Write(emrbr_);
        w.Write(erdsr_); w.Write(etdsr_); w.Write(emrbr2_);
        w.Write(phy_bmcr_);
        rings_.SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        r.Read(eir_); r.Read(eimr_); r.Read(ecr_);
        r.Read(rcr_); r.Read(tcr_); r.Read(mmfr_); r.Read(mscr_);
        r.Read(mibc_); r.Read(iaur_); r.Read(ialr_); r.Read(gaur_); r.Read(galr_);
        r.Read(palr_); r.Read(paur_); r.Read(opd_);
        r.Read(tfwr_); r.Read(frbr_); r.Read(frsr_); r.Read(emrbr_);
        r.Read(erdsr_); r.Read(etdsr_); r.Read(emrbr2_);
        r.Read(phy_bmcr_);
        rings_.RestoreState(r);
    }

private:
    static constexpr uint32_t kBase = 0x02188000u;
    static constexpr uint32_t kSize = 0x4000u;

    static constexpr uint32_t kEir  = 0x004u;
    static constexpr uint32_t kEimr = 0x008u;
    static constexpr uint32_t kRdar = 0x010u;
    static constexpr uint32_t kTdar = 0x014u;
    static constexpr uint32_t kEcr  = 0x024u;
    static constexpr uint32_t kMmfr = 0x040u;
    static constexpr uint32_t kMscr = 0x044u;
    static constexpr uint32_t kMibc = 0x064u;
    static constexpr uint32_t kRcr  = 0x084u;
    static constexpr uint32_t kTcr  = 0x0C4u;
    static constexpr uint32_t kPalr = 0x0E4u;
    static constexpr uint32_t kPaur = 0x0E8u;
    static constexpr uint32_t kOpd  = 0x0ECu;
    static constexpr uint32_t kIaur = 0x118u;
    static constexpr uint32_t kIalr = 0x11Cu;
    static constexpr uint32_t kGaur = 0x120u;
    static constexpr uint32_t kGalr = 0x124u;
    static constexpr uint32_t kTfwr = 0x144u;
    static constexpr uint32_t kFrbr = 0x14Cu;
    static constexpr uint32_t kFrsr = 0x150u;
    static constexpr uint32_t kErdSr = 0x180u;
    static constexpr uint32_t kEtdSr = 0x184u;
    static constexpr uint32_t kEmrbr = 0x188u;
    static constexpr uint32_t kErswR = 0x190u;
    static constexpr uint32_t kRmonBase = 0x200u;
    static constexpr uint32_t kIeeeBase = 0x2C0u;

    static constexpr uint32_t kEcrReset = 0x00000001u;
    static constexpr uint32_t kEcrEtherEn = 0x00000002u;

    static constexpr uint32_t kEirMii = 0x00800000u;
    uint32_t ReadReg(uint32_t off) {
        switch (off) {
        case kEir:  return eir_;
        case kEimr: return eimr_;
        case kRdar: return rings_.Rdar();
        case kTdar: return rings_.Tdar();
        case kEcr:  return ecr_ & ~kEcrReset;  /* hardware self-clears reset */
        case kMmfr: return mmfr_;
        case kMscr: return mscr_;
        case kMibc: return mibc_;
        case kRcr:  return rcr_;
        case kTcr:  return tcr_;
        case kPalr: return palr_;
        case kPaur: return paur_;
        case kOpd:  return opd_;
        case kIaur: return iaur_;
        case kIalr: return ialr_;
        case kGaur: return gaur_;
        case kGalr: return galr_;
        case kTfwr: return tfwr_;
        case kFrbr: return frbr_;
        case kFrsr: return frsr_;
        case kErdSr: return erdsr_;
        case kEtdSr: return etdsr_;
        case kEmrbr: return emrbr_;
        case kErswR: return 0u;
        default:
            if ((off >= kRmonBase && off < kRmonBase + 0x80u)
             || (off >= kIeeeBase && off < kIeeeBase + 0x40u)) {
                return 0u;  /* MIB/RMON counters reset to zero. */
            }
            HaltUnsupportedAccess("imx6-fec read32 unmodelled register", kBase + off, 0);
        }
    }

    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
        case kEir:
            eir_ &= ~value;  /* write-one-to-clear */
            UpdateIrq();
            return;
        case kEimr:
            eimr_ = value;
            UpdateIrq();
            return;
        case kRdar:
            rings_.RequestReceive(emu_.Get<EmulatedMemory>(),
                                  (ecr_ & kEcrEtherEn) != 0u);
            return;
        case kTdar:
            eir_ |= rings_.RequestTransmit(emu_.Get<EmulatedMemory>(),
                                           emu_.Get<NetworkBackend>(),
                                           (ecr_ & kEcrEtherEn) != 0u,
                                           kCableConnected, etdsr_);
            UpdateIrq();
            return;
        case kEcr:
            if (value & kEcrReset) {
                ResetController();
                return;
            }
            ecr_ = value & ~kEcrReset;
            if ((ecr_ & kEcrEtherEn) == 0u)
                rings_.Disable(erdsr_, etdsr_);
            return;
        case kMmfr:
            mmfr_ = value;
            CompleteMiiTransaction();
            return;
        case kMscr: mscr_ = value; return;
        case kMibc:
            if (value & 0x20000000u) {
                value &= ~0x20000000u; /* MIB_CLEAR self-clears */
            }
            mibc_ = value;
            return;
        case kRcr:  rcr_ = value; return;
        case kTcr:  tcr_ = value; return;
        case kPalr: palr_ = value; return;
        case kPaur: paur_ = (value | 0x0000FFFFu) & 0xFFFF8808u; return;
        case kOpd:  opd_ = (value & 0x0000FFFFu) | 0x00010000u; return;
        case kIaur: iaur_ = value; return;
        case kIalr: ialr_ = value; return;
        case kGaur: gaur_ = value; return;
        case kGalr: galr_ = value; return;
        case kTfwr: tfwr_ = value; return;
        case kFrbr: frbr_ = value; return;
        case kFrsr: frsr_ = value; return;
        case kErdSr:
            erdsr_ = value & ~7u;
            rings_.SetRxDescriptorBase(erdsr_);
            return;
        case kEtdSr:
            etdsr_ = value & ~7u;
            rings_.SetTxDescriptorBase(etdsr_);
            return;
        case kEmrbr:
            emrbr_ = value & 0x00003FF0u;
            emrbr2_ = emrbr_;
            return;
        case kErswR: return;
        default:
            if ((off >= kRmonBase && off < kRmonBase + 0x80u)
             || (off >= kIeeeBase && off < kIeeeBase + 0x40u)) {
                return;
            }
            HaltUnsupportedAccess("imx6-fec write32 unmodelled register", kBase + off, value);
        }
    }

    void ResetController() {
        eir_ = 0u;
        eimr_ = 0u;
        ecr_ = 0xF0000000u;
        rcr_ = 0x05EE0001u;  /* reset-ish MAX_FL=1518 plus loopback/promisc clear */
        tcr_ = 0u;
        mmfr_ = 0u;
        mscr_ = 0u;
        mibc_ = 0xC0000000u;
        paur_ = (paur_ & 0xFFFF0000u) | 0x00008808u;
        opd_ = 0x00010000u;
        tfwr_ = 0u;
        frbr_ = 0x00000600u;
        frsr_ = 0x00000500u;
        erdsr_ = 0u;
        etdsr_ = 0u;
        emrbr_ = 0u;
        emrbr2_ = 0u;
        phy_bmcr_ = 0x1140u;
        rings_.Reset();
        UpdateIrq();
    }

    void OnHostFrame(const uint8_t* frame, std::size_t len) {
        if (!frame || len < 14u)
            return;
        if (!kCableConnected)
            return;
        if (len > 1518u)
            len = 1518u;

        std::lock_guard<std::mutex> lk(mtx_);
        if ((ecr_ & kEcrEtherEn) == 0u || erdsr_ == 0u || emrbr_ == 0u)
            return;
        if (!AcceptFrame(frame, len))
            return;

        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t events = rings_.Receive(mem, frame, len, erdsr_, emrbr_);
        if (events != 0u) {
            eir_ |= events;
            UpdateIrq();
        }
    }

    bool AcceptFrame(const uint8_t* frame, std::size_t len) const {
        if (len < 14u)
            return false;
        const bool broadcast =
            frame[0] == 0xFFu && frame[1] == 0xFFu && frame[2] == 0xFFu &&
            frame[3] == 0xFFu && frame[4] == 0xFFu && frame[5] == 0xFFu;
        const bool multicast = (frame[0] & 1u) != 0u;
        const bool unicast =
            std::equal(guest_mac_.begin(), guest_mac_.end(), frame);
        if (broadcast || multicast || unicast)
            return true;
        return (rcr_ & (1u << 3)) != 0u; /* PROM */
    }

    void CompleteMiiTransaction() {
        const uint32_t op  = (mmfr_ >> 28) & 3u;
        const uint32_t phy = (mmfr_ >> 23) & 0x1Fu;
        const uint32_t reg = (mmfr_ >> 18) & 0x1Fu;

        /* FEC MMFR is the IEEE 802.3 MII management frame.  QEMU's i.MX6
           machine wires a board PHY on the MDIO bus (phy-num defaults to 0);
           expose a small standard PHY register set so BSP probing reads real
           MDIO data instead of the command word echo.  Microchip KSZ9021RL/RN
           DS00003050A, Basic Status register: BMSR[2] reports link and BMSR[5]
           reports autonegotiation completion. */
        if (op == 2u) { /* read */
            mmfr_ = (mmfr_ & 0xFFFF0000u) | ReadPhyRegister(phy, reg);
        } else if (op == 1u && phy == kPhyAddr) { /* write */
            WritePhyRegister(reg, static_cast<uint16_t>(mmfr_));
        }
        eir_ |= kEirMii;
        UpdateIrq();
    }

    uint16_t ReadPhyRegister(uint32_t phy, uint32_t reg) const {
        if (phy != kPhyAddr) return 0xFFFFu;
        switch (reg) {
        case 0x00: return phy_bmcr_;   /* BMCR: autoneg enabled, normal op */
        case 0x01: return kCableConnected
            ? 0x786Du                  /* BMSR: 10/100, autoneg complete, link up */
            : 0x7849u;                 /* BMSR: 10/100 capable, autoneg able, link down */
        case 0x02: return 0x0022u;     /* PHYIDR1: Micrel/Microchip OUI high */
        case 0x03: return 0x1611u;     /* PHYIDR2: BSP-supported KSZ9021 (ID 0x00221611) */
        case 0x04: return 0x01E1u;     /* advertise 10/100 half/full */
        case 0x05: return kCableConnected
            ? 0x45E1u                  /* link partner ability + ACK */
            : 0x0000u;                 /* no link partner while cable is unplugged */
        case 0x1F: return 0x0000u;
        default:   return 0x0000u;
        }
    }

    void WritePhyRegister(uint32_t reg, uint16_t value) {
        if (reg == 0x00) {
            if (value & 0x8000u) {     /* reset self-clears */
                phy_bmcr_ = 0x1140u;
            } else {
                phy_bmcr_ = value;
            }
        }
    }

    void UpdateIrq() {
        /* Linux imx6qdl.dtsi and QEMU fsl-imx6 wire ENET MAC interrupt to
           GIC SPI 118.  The FEC raises the line when any enabled EIR bit is
           pending; EIR bits are W1C. */
        if ((eir_ & eimr_) != 0u)
            emu_.Get<Imx6Gic>().AssertSpi(118);
        else
            emu_.Get<Imx6Gic>().DeAssertSpi(118);
    }

    uint32_t eir_ = 0u;
    uint32_t eimr_ = 0u;
    uint32_t ecr_ = 0xF0000000u;
    uint32_t rcr_ = 0x05EE0001u;
    uint32_t tcr_ = 0u;
    uint32_t mmfr_ = 0u;
    uint32_t mscr_ = 0u;
    uint32_t mibc_ = 0xC0000000u;
    uint32_t iaur_ = 0u;
    uint32_t ialr_ = 0u;
    uint32_t gaur_ = 0u;
    uint32_t galr_ = 0u;
    uint32_t palr_ = 0x02000000u;
    uint32_t paur_ = 0x00008808u;
    uint32_t opd_ = 0x00010000u;
    uint32_t tfwr_ = 0u;
    uint32_t frbr_ = 0x00000600u;
    uint32_t frsr_ = 0x00000500u;
    uint32_t erdsr_ = 0u;
    uint32_t etdsr_ = 0u;
    uint32_t emrbr_ = 0u;
    uint32_t emrbr2_ = 0u;
    Imx6FecLegacyRing rings_;
    std::array<uint8_t, 6> guest_mac_{};
    bool rx_installed_ = false;
    mutable std::mutex mtx_;
    static constexpr uint32_t kPhyAddr = 0u;
    static constexpr bool kCableConnected = true;
    uint16_t phy_bmcr_ = 0x1140u;
};

}  // namespace

REGISTER_SERVICE(Imx6Fec);

