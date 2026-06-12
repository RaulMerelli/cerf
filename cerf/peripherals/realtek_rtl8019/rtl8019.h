#pragma once

#include "../pcmcia/pcmcia_card.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/* NE2000-compatible PCMCIA Ethernet card. NetworkBackend carries a
   single RX consumer: two simultaneously inserted Rtl8019 cards steal
   each other's RX stream, and rx_installed_ keeps eject from tearing
   down a sibling's callback. */
class Rtl8019 : public PcmciaCard {
public:
    static constexpr const wchar_t* kDisplayName =
        L"NE2000 Ethernet (RTL8019)";

    explicit Rtl8019(CerfEmulator& emu);
    ~Rtl8019() override;

    std::wstring DisplayName() const override { return kDisplayName; }
    std::wstring TooltipDetail() const override;

    void OnInserted() override;
    void OnShutdown() override;

    void PowerOn () override;
    void PowerOff() override;

    uint8_t  ReadAttribute8 (uint32_t offset)                override;
    void     WriteAttribute8(uint32_t offset, uint8_t value) override;

    uint8_t  ReadCommon8  (uint32_t offset)                  override;
    uint16_t ReadCommon16 (uint32_t offset)                  override;
    void     WriteCommon8 (uint32_t offset, uint8_t  value)  override;
    void     WriteCommon16(uint32_t offset, uint16_t value)  override;

    uint8_t  ReadIo8  (uint32_t offset)                      override;
    uint16_t ReadIo16 (uint32_t offset)                      override;
    void     WriteIo8 (uint32_t offset, uint8_t  value)      override;
    void     WriteIo16(uint32_t offset, uint16_t value)      override;

    std::vector<WidgetMenuItem> BuildCardMenu() override;

    const char* SaveId() const override { return "ne2000"; }
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void DetachRx();
    void OnRxFrame(const uint8_t* frame, std::size_t len);

    void TransmitFromCardRamLocked(std::vector<uint8_t>& out_frame);

    /* Update NIC_INTR_STATUS + drive the slot IRQ line. Callers hold
       state_mutex_; the slot line call-out happens after computing the
       mask (slot routes to the controller, which has its own lock). */
    void RaiseInterruptLocked(uint8_t bits);
    void ClearInterruptIfDrainedLocked();

    /* Full NIC_RESET behavior — guest writes to NIC_RESET (offset
       0x1F) and PowerOn both route here. */
    void ResetLocked();

    bool ShouldIndicateMulticastPacketLocked(const uint8_t* dest_mac) const;

    /* MAC from NetworkBackend, also imaged into CardROM so the driver
       reads it via the CIS path. */
    std::array<uint8_t, 6> guest_mac_{};
    bool rx_installed_ = false;

    mutable std::mutex state_mutex_;

    /* NIC_COMMAND register at offset 0 — selects page for offsets
       1..0x0F via bits 6-7. */
    uint8_t nic_command_ = 0u;

    /* Page 0 — control + RX/TX. */
    uint8_t  nic_page_start_  = 0u;
    uint8_t  nic_page_stop_   = 0u;
    uint8_t  nic_boundary_    = 0u;
    uint8_t  nic_xmit_status_ = 0u;
    uint8_t  nic_xmit_start_  = 0u;
    uint16_t nic_xmit_count_  = 0u;
    uint8_t  nic_fifo_        = 0u;
    uint8_t  nic_intr_status_ = 0u;
    uint16_t nic_crda_        = 0u;
    uint16_t nic_rmt_addr_    = 0u;
    uint16_t nic_rmt_count_   = 0u;
    uint8_t  nic_rcv_config_  = 0u;
    uint8_t  nic_rcv_status_  = 0u;
    uint8_t  nic_xmit_config_ = 0u;
    uint8_t  nic_fae_err_     = 0u;
    uint8_t  nic_data_config_ = 0u;
    uint8_t  nic_crc_err_     = 0u;
    uint8_t  nic_intr_mask_   = 0u;
    uint8_t  nic_missed_cnt_  = 0u;

    /* Page 1 — physical MAC + multicast filter + current page. */
    std::array<uint8_t, 6> nic_phys_addr_{};
    std::array<uint8_t, 8> nic_mc_addr_{};
    uint8_t                nic_current_ = 0u;

    /* DMA state for the NIC_RACK_NIC remote-DMA register at offset
       0x10 — driver sets nic_rmt_addr_/nic_rmt_count_ + CR_DMA_*,
       then byte-streams through this register. */
    uint16_t dma_count_  = 0u;
    uint16_t dma_offset_ = 0u;

    /* FCSR (Function Control / Status Register) at attribute offset
       0x3FA — PCMCIA function register the driver reads to check
       whether the on-card INT is pending. */
    uint8_t fcsr_ = 0u;

    /* COR at attribute 0x3F8 (CIS CONFIG tuple). Its low 6 bits pick
       the CFTABLE entry (0x20-0x23 → I/O base 0x300/0x320/0x340/0x360);
       the card decodes no I/O until a valid index is written. */
    uint8_t cor_ = 0u;
    bool MapCardIoLocked(uint32_t card_io, uint32_t* reg) const;

    static constexpr std::size_t kCardRomSize = 32;
    std::array<uint8_t, kCardRomSize> card_rom_{};

    /* On-card RAM at common-memory offset 0x4000, 16 KB. RX writes
       4-byte prefix + frame bytes into here at NIC_CURRENT << 8;
       TX reads from NIC_XMIT_START << 8. */
    static constexpr uint32_t kRamBase = 0x4000u;
    static constexpr uint32_t kRamSize = 0x4000u;
    std::array<uint8_t, kRamSize> card_ram_{};
};
