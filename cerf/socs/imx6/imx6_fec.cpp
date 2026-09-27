#include "imx6_gic.h"

#include "imx6_fec.h"
#include "../../core/crc32.h"
#include "imx6_fec_legacy_ring.h"
#include "imx6_mmio_lane.h"

#include "../../boards/board_context.h"
#include "../../cpu/emulated_memory.h"
#include "../../core/cerf_emulator.h"
#include "../../core/fatal.h"
#include "../../net/network_backend.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include "imx6_id.h"

namespace {

/* Linux imx6qdl.dtsi fec: ethernet@2188000, reg <0x02188000 0x4000>, GIC SPI 118. IMX6DQRM Rev.2 §23.5:
   EIR 0x004, EIMR 0x008, RDAR 0x010, TDAR 0x014, ECR 0x024, MMFR 0x040, MSCR 0x044,
   PALR 0x0E4, PAUR 0x0E8. */
class Imx6Fec final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }

    void OnReady() override {
        guest_mac_ = emu_.Get<NetworkBackend>().AttachReceiver(
            kImx6FecReceiverId, NetworkBackend::ReceiverKind::Ethernet,
            [this](const uint8_t* frame, std::size_t len) { OnHostFrame(frame, len); });
        rx_installed_ = true;
        palr_ = (uint32_t(guest_mac_[0]) << 24) | (uint32_t(guest_mac_[1]) << 16) | (uint32_t(guest_mac_[2]) << 8) |
                uint32_t(guest_mac_[3]);
        paur_ = (uint32_t(guest_mac_[4]) << 24) | (uint32_t(guest_mac_[5]) << 16) | 0x00008808u;
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
    }

    void OnShutdown() override {
        if (rx_installed_) {
            emu_.Get<NetworkBackend>().DetachReceiver(kImx6FecReceiverId);
            rx_installed_ = false;
        }
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override { return ReadReg(addr - kBase); }

    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override { WriteReg(addr - kBase, value); }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.Write("eir", eir_);
        w.Write("eimr", eimr_);
        w.Write("ecr", ecr_);
        w.Write("rcr", rcr_);
        w.Write("tcr", tcr_);
        w.Write("mmfr", mmfr_);
        w.Write("mscr", mscr_);
        w.Write("iaur", iaur_);
        w.Write("ialr", ialr_);
        w.Write("gaur", gaur_);
        w.Write("galr", galr_);
        w.Write("palr", palr_);
        w.Write("paur", paur_);
        w.Write("tfwr", tfwr_);
        w.Write("emrbr", emrbr_);
        w.Write("erdsr", erdsr_);
        w.Write("etdsr", etdsr_);
        w.Write("phy_bmcr", phy_bmcr_);
        w.Write("phy_gbcr", phy_gbcr_);
        w.Write("phy_ext_address", phy_ext_address_);
        rings_.SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        r.Read("eir", eir_);
        r.Read("eimr", eimr_);
        r.Read("ecr", ecr_);
        r.Read("rcr", rcr_);
        r.Read("tcr", tcr_);
        r.Read("mmfr", mmfr_);
        r.Read("mscr", mscr_);
        r.Read("iaur", iaur_);
        r.Read("ialr", ialr_);
        r.Read("gaur", gaur_);
        r.Read("galr", galr_);
        r.Read("palr", palr_);
        r.Read("paur", paur_);
        r.Read("tfwr", tfwr_);
        r.Read("emrbr", emrbr_);
        r.Read("erdsr", erdsr_);
        r.Read("etdsr", etdsr_);
        r.Read("phy_bmcr", phy_bmcr_);
        r.Read("phy_gbcr", phy_gbcr_);
        r.Read("phy_ext_address", phy_ext_address_);
        rings_.RestoreState(r);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        UpdateIrq();
    }

private:
    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - kBase;
        if (off == kEir) {
            eir_ &= ~lane.value;
            UpdateIrq();
            return;
        }
        WriteReg(off, lane.Merge(ReadReg(off)));
    }

    static constexpr uint32_t kBase = 0x02188000u;
    static constexpr uint32_t kSize = 0x4000u;

    static constexpr uint32_t kEir = 0x004u;
    static constexpr uint32_t kEimr = 0x008u;
    static constexpr uint32_t kRdar = 0x010u;
    static constexpr uint32_t kTdar = 0x014u;
    static constexpr uint32_t kEcr = 0x024u;
    static constexpr uint32_t kMmfr = 0x040u;
    static constexpr uint32_t kMscrSpeedMask = 0x0000003Eu;
    static constexpr uint32_t kTfwrStoreAndForward = 0x00000100u;
    static constexpr uint32_t kMscr = 0x044u;
    static constexpr uint32_t kRcr = 0x084u;
    /* IMX6DQRM Rev.2 §23.5.9: RCR PROM is bit 3 and BC_REJ bit 4. */
    static constexpr uint32_t kRcrProm = 1u << 3;
    static constexpr uint32_t kRcrBcRej = 1u << 4;
    static constexpr uint32_t kTcr = 0x0C4u;
    static constexpr uint32_t kTcrFden = 1u << 2;
    static constexpr uint32_t kPalr = 0x0E4u;
    static constexpr uint32_t kPaur = 0x0E8u;
    static constexpr uint32_t kIaur = 0x118u;
    static constexpr uint32_t kIalr = 0x11Cu;
    static constexpr uint32_t kGaur = 0x120u;
    static constexpr uint32_t kGalr = 0x124u;
    static constexpr uint32_t kTfwr = 0x144u;
    static constexpr uint32_t kErdSr = 0x180u;
    static constexpr uint32_t kEtdSr = 0x184u;
    static constexpr uint32_t kEmrbr = 0x188u;

    static constexpr uint32_t kEcrReset = 0x00000001u;
    static constexpr uint32_t kEcrEtherEn = 0x00000002u;

    static constexpr uint32_t kEirMii = 0x00800000u;
    uint32_t ReadReg(uint32_t off) {
        switch (off) {
        case kEir: return eir_;
        case kEimr: return eimr_;
        case kRdar: return rings_.Rdar();
        case kTdar: return rings_.Tdar();
        case kEcr: return ecr_ & ~kEcrReset;
        case kMmfr: return mmfr_;
        case kMscr: return mscr_;
        case kRcr: return rcr_;
        case kTcr: return tcr_;
        case kPalr: return palr_;
        case kPaur: return paur_;
        case kIaur: return iaur_;
        case kIalr: return ialr_;
        case kGaur: return gaur_;
        case kGalr: return galr_;
        case kTfwr: return tfwr_;
        case kErdSr: return erdsr_;
        case kEtdSr: return etdsr_;
        case kEmrbr: return emrbr_;
        default: HaltUnsupportedAccess("imx6-fec read32 unmodelled register", kBase + off, 0);
        }
    }

    void WriteReg(uint32_t off, uint32_t value) {
        switch (off) {
        case kEir:
            eir_ &= ~value;
            UpdateIrq();
            return;
        case kEimr:
            eimr_ = value;
            UpdateIrq();
            return;
        case kRdar: rings_.RequestReceive(emu_.Get<EmulatedMemory>(), (ecr_ & kEcrEtherEn) != 0u); return;
        case kTdar:
            eir_ |= rings_.RequestTransmit(emu_.Get<EmulatedMemory>(), emu_.Get<NetworkBackend>(),
                                           (ecr_ & kEcrEtherEn) != 0u, LinkIsUp(), etdsr_);
            UpdateIrq();
            return;
        case kEcr:
            if (value & kEcrReset) {
                ResetController();
                return;
            }
            ecr_ = value & ~kEcrReset;
            if ((ecr_ & kEcrEtherEn) == 0u) rings_.Disable(erdsr_, etdsr_);
            return;
        case kMmfr:
            mmfr_ = value;
            CompleteMiiTransaction();
            return;
        case kMscr: mscr_ = value; return;
        case kRcr: rcr_ = value; return;
        /* IMX6DQRM Rev.2 §23.5.10: GTS and TFC_PAUSE stop transmission and set EIR[GRA], ADDINS
           overwrites the source MAC address, CRCFWD suppresses the appended CRC, and FDEN makes
           frames transmit independent of carrier sense and collision inputs. */
        case kTcr:
            if ((value & ~kTcrFden) != 0u)
                emu_.Get<Fatal>().Die("[FEC] TCR 0x%08X beyond FDEN is not modelled", value);
            tcr_ = value;
            return;
        case kPalr: palr_ = value; return;
        case kPaur: paur_ = (value | 0x0000FFFFu) & 0xFFFF8808u; return;
        case kIaur: iaur_ = value; return;
        case kIalr: ialr_ = value; return;
        case kGaur: gaur_ = value; return;
        case kGalr: galr_ = value; return;
        /* IMX6DQRM Rev.2 §23.5.18: with STRFWD clear the MAC starts transmission once the FIFO
           reaches TFWR, before the end of frame is available; this model hands whole frames over. */
        case kTfwr:
            if ((value & kTfwrStoreAndForward) == 0u)
                emu_.Get<Fatal>().Die("[FEC] TFWR cut-through is not modelled (0x%08X)", value);
            tfwr_ = value;
            return;
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
            return;
        default: HaltUnsupportedAccess("imx6-fec write32 unmodelled register", kBase + off, value);
        }
    }

    void ResetController() {
        eir_ = 0u;
        eimr_ = 0u;
        ecr_ = 0xF0000000u;
        rcr_ = 0x05EE0001u;
        tcr_ = 0u;
        mmfr_ = 0u;
        mscr_ = 0u;
        paur_ = (paur_ & 0xFFFF0000u) | 0x00008808u;
        tfwr_ = 0u;
        erdsr_ = 0u;
        etdsr_ = 0u;
        emrbr_ = 0u;
        rings_.Reset();
        UpdateIrq();
    }

    void OnHostFrame(const uint8_t* frame, std::size_t len) {
        if (!frame || len < 14u) return;
        if (!LinkIsUp()) return;
        if (len > 1518u) len = 1518u;

        std::lock_guard<std::mutex> lk(mtx_);
        if ((ecr_ & kEcrEtherEn) == 0u || erdsr_ == 0u || emrbr_ == 0u) return;
        if (!AcceptFrame(frame, len)) return;

        auto& mem = emu_.Get<EmulatedMemory>();
        const uint32_t events = rings_.Receive(mem, frame, len, erdsr_, emrbr_);
        if (events != 0u) {
            eir_ |= events;
            UpdateIrq();
        }
    }

    /* IMX6DQRM Rev.2 §23.6.4.3.2: the six most significant bits of the CRC-32 of the destination
       address index the 64-bit table, the top one choosing the upper half; Table 23-126 accepts a
       broadcast unless BC_REJ is set without PROM. */
    bool AcceptFrame(const uint8_t* frame, std::size_t len) const {
        if (len < 14u) return false;
        const bool promiscuous = (rcr_ & kRcrProm) != 0u;
        const bool broadcast = frame[0] == 0xFFu && frame[1] == 0xFFu && frame[2] == 0xFFu && frame[3] == 0xFFu &&
                               frame[4] == 0xFFu && frame[5] == 0xFFu;
        if (broadcast) return promiscuous || (rcr_ & kRcrBcRej) == 0u;
        if (promiscuous) return true;
        const bool multicast = (frame[0] & 1u) != 0u;
        /* IMX6DQRM Rev.2 §23.6.4.3.4 names the node address as PALR[PADDR1] and PAUR[PADDR2]. */
        const uint32_t da_lo = (static_cast<uint32_t>(frame[0]) << 24) | (static_cast<uint32_t>(frame[1]) << 16) |
                               (static_cast<uint32_t>(frame[2]) << 8) | static_cast<uint32_t>(frame[3]);
        const uint32_t da_hi = (static_cast<uint32_t>(frame[4]) << 24) | (static_cast<uint32_t>(frame[5]) << 16);
        if (!multicast && da_lo == palr_ && da_hi == (paur_ & 0xFFFF0000u)) return true;
        /* IMX6DQRM Rev.2 §23.6.4.3.2 takes the hash index from the upper six bits of the CRC over
           the destination address; Linux fec_main.c feeds ether_crc_le, which is the accumulator
           before the final inversion, into the same six bits. */
        const uint32_t index = cerf::Crc32Accumulator(frame, 6u) >> 26u;
        const uint32_t table = (index & 0x20u) != 0u ? (multicast ? gaur_ : iaur_) : (multicast ? galr_ : ialr_);
        return ((table >> (index & 0x1Fu)) & 1u) != 0u;
    }

    /* IMX6DQRM Rev.2 §23.5.7: "The MII_SPEED must be set to a non-zero value to source a read or
       write management frame", and the register may be cleared afterwards to turn off MDC. */
    void CompleteMiiTransaction() {
        if ((mscr_ & kMscrSpeedMask) == 0u) return;
        const uint32_t op = (mmfr_ >> 28) & 3u;
        const uint32_t phy = (mmfr_ >> 23) & 0x1Fu;
        const uint32_t reg = (mmfr_ >> 18) & 0x1Fu;

        /* QEMU hw/arm/fsl-imx6.c: fec-phy-num; KSZ9021RL/RN DS00003050A section 4.1. */
        if (op == 2u) {
            mmfr_ = (mmfr_ & 0xFFFF0000u) | ReadPhyRegister(phy, reg);
        } else if (op == 1u) {
            if (phy == kPhyAddr) WritePhyRegister(reg, static_cast<uint16_t>(mmfr_));
        } else {
            emu_.Get<Fatal>().Die("[FEC] MII operation %u is not modelled (MMFR 0x%08X)", op, mmfr_);
        }
        eir_ |= kEirMii;
        UpdateIrq();
    }

    bool MasterSlaveResolution() const {
        return (phy_gbcr_ & 0x1000u) ? ((phy_gbcr_ & 0x0800u) != 0u) : ((phy_gbcr_ & 0x0400u) != 0u);
    }

    uint16_t ReadPhyRegister(uint32_t phy, uint32_t reg) const {
        if (phy != kPhyAddr) return 0xFFFFu;
        switch (reg) {
        case 0x00: return phy_bmcr_;
        case 0x01:
            return LinkIsUp() ? 0x796Du : 0x7949u;
        /* KSZ9021RL/RN DS00003050A §4.1: registers 2 and 3 carry the Kendin OUI 0010A1, model
           number 100001b and a silicon revision. */
        case 0x02: return 0x0022u;
        case 0x03: return 0x1611u;
        /* KSZ9021RL/RN DS00003050A §4.1 register 4: 100Base-TX and 10Base-T, full and half duplex,
           with the IEEE 802.3 selector field. */
        case 0x04: return 0x01E1u;
        /* KSZ9021RL/RN DS00003050A §4.1 register 5: acknowledge, asymmetric pause and the same four
           speed abilities are what the modelled link partner answers with. */
        case 0x05:
            return LinkIsUp() ? 0x45E1u : 0x0000u;
        /* KSZ9021RL/RN DS00003050A §4.1 register 31: Enable Jabber defaults to 1, and 31.5 and 31.3
           report the resolved speed and duplex, which for the modelled link partner is
           100Base-TX full duplex. */
        case 0x1F: return LinkIsUp() ? 0x0228u : 0x0200u;
        /* KSZ9021RL/RN DS00003050A §4.1 register 9: bits 7:0 are reserved, "write as 0, ignore on read". */
        case 0x09: return static_cast<uint16_t>(phy_gbcr_ & 0xFF00u);
        /* KSZ9021RL/RN DS00003050A §4.1 register 10: 10.14 resolves to MASTER from 9.11 when 9.12
           enables the manual configuration and from the port-type preference in 9.10 otherwise;
           10.11 and 10.10 report the link partner's 1000Base-T abilities. */
        case 0x0A: return MasterSlaveResolution() ? 0x4000u : 0x0000u;
        /* KSZ9021RL/RN DS00003050A §4.1 register 15: 1000Base-T full and half duplex, which is the
           Extended Status that BMSR bit 8 advertises. */
        case 0x0F: return 0x3000u;
        case 0x0D:
            emu_.Get<Fatal>().Die("[FEC] MDIO read of extended register 0x%03X is not modelled", phy_ext_address_);
        default: emu_.Get<Fatal>().Die("[FEC] MDIO read of unmodelled PHY register %u", reg);
        }
    }

    /* KSZ9021RL/RN DS00003050A §4.1: register 11 is Extended Register Control with bit 15
       selecting a write, 12 is Data Write and 13 Data Read; 260..262 are the RGMII pad skews.
       The same map lists 14, 29 and 30 as Reserved. */
    void WritePhyRegister(uint32_t reg, uint16_t value) {
        switch (reg) {
        case 0x00: phy_bmcr_ = (value & 0x8000u) ? 0x1140u : value; return;
        /* KSZ9021RL/RN DS00003050A §4.1 register 9: bits 15:13 select the transmitter test modes. */
        case 0x09:
            if ((value & 0xE000u) != 0u)
                emu_.Get<Fatal>().Die("[FEC] PHY 1000Base-T test mode 0x%04X is not modelled", value);
            phy_gbcr_ = value;
            return;
        case 0x0B: phy_ext_address_ = value & 0x0FFFu; return;
        case 0x0C: WriteExtendedRegister(phy_ext_address_, value); return;
        case 0x0D:
        case 0x0E:
        case 0x1D:
        case 0x1E: return;
        default:
            emu_.Get<Fatal>().Die("[FEC] MDIO write of unmodelled PHY register %u value 0x%04X", reg, value);
        }
    }

    void WriteExtendedRegister(uint32_t reg, uint16_t value) {
        switch (reg) {
        /* KSZ9021RL/RN DS00003050A §4.1: 260..262 are the RGMII clock and data pad skews, PCB
           timing that an emulated link does not reproduce. */
        case 0x104:
        case 0x105:
        case 0x106: return;
        default:
            emu_.Get<Fatal>().Die("[FEC] MDIO write of unmodelled extended register 0x%03X value 0x%04X", reg, value);
        }
    }


    bool LinkIsUp() const { return rx_installed_; }

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
    uint32_t iaur_ = 0u;
    uint32_t ialr_ = 0u;
    uint32_t gaur_ = 0u;
    uint32_t galr_ = 0u;
    uint32_t palr_ = 0x02000000u;
    uint32_t paur_ = 0x00008808u;
    uint32_t tfwr_ = 0u;
    uint32_t erdsr_ = 0u;
    uint32_t etdsr_ = 0u;
    uint32_t emrbr_ = 0u;
    Imx6FecLegacyRing rings_;
    std::array<uint8_t, 6> guest_mac_{};
    bool rx_installed_ = false;
    mutable std::mutex mtx_;
    static constexpr uint32_t kPhyAddr = 0u;
    uint16_t phy_bmcr_ = 0x1140u;
    /* KSZ9021RL/RN DS00003050A §4.1 register 9: the 1000Base-T full-duplex advertisement
       defaults to 1. */
    uint16_t phy_gbcr_ = 0x0200u;
    uint32_t phy_ext_address_ = 0u;
};

}

REGISTER_SERVICE(Imx6Fec);
