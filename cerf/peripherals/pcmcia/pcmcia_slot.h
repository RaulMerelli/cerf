#pragma once

#include "pcmcia_card.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class CerfEmulator;
class PcmciaSlot;

/* Implemented by the socket controller that owns the slot. Callbacks
   arrive WITHOUT the slot's bus lock held, on the inserting/ejecting
   thread (UI or boot) for detect changes and on the card's signaling
   thread for IRQs. */
class PcmciaSlotHost {
public:
    virtual ~PcmciaSlotHost() = default;
    virtual void OnCardDetectChanged(PcmciaSlot& slot) = 0;
    virtual void OnCardIrqAsserted  (PcmciaSlot& slot) = 0;
    virtual void OnCardIrqDeasserted(PcmciaSlot& slot) = 0;
};

/* One physical PCMCIA socket. Lock hierarchy: bus_mutex_ > backend
   rx-callback mutex > card mutex > controller mutex. RaiseIrq/ClearIrq
   and PcmciaSlotHost callbacks must stay off bus_mutex_ - eject holds
   it while the card dtor drains in-flight RX, which runs RaiseIrq. */
class PcmciaSlot : public HostWidget {
public:
    PcmciaSlot(CerfEmulator& emu, PcmciaSlotHost& host, std::wstring label);

    /* Guest bus surface. Empty or unpowered socket reads float high
       (PCMCIA bus convention: no card drives the data lines). */
    bool HasCard()   const;
    bool IsPowered() const;
    void SetPowered(bool on);
    void ResetCard();          /* socket RESET pin pulse */

    uint8_t  ReadAttribute8 (uint32_t offset);
    void     WriteAttribute8(uint32_t offset, uint8_t value);
    uint8_t  ReadCommon8  (uint32_t offset);
    uint16_t ReadCommon16 (uint32_t offset);
    void     WriteCommon8 (uint32_t offset, uint8_t  value);
    void     WriteCommon16(uint32_t offset, uint16_t value);
    uint8_t  ReadIo8  (uint32_t offset);
    uint16_t ReadIo16 (uint32_t offset);
    void     WriteIo8 (uint32_t offset, uint8_t  value);
    void     WriteIo16(uint32_t offset, uint16_t value);

    /* Card-side IRQ line, callable from any card thread; no bus lock. */
    void RaiseIrq();
    void ClearIrq();

    /* Insert/eject. Used by the UI menu and by board boot-default
       services. Inserting into an occupied slot is rejected loudly. */
    void InsertCard(std::unique_ptr<PcmciaCard> card);
    void EjectCard();

    /* Forward the CerfEmulator quiesce phase to the resident card so it can
       detach from peers before any service is destroyed. Owning controllers
       call this from their Service::OnShutdown. */
    void OnShutdown();

    /* NOT named SaveState/RestoreState: that overrides HostWidget::RestoreState,
       so the Widget hibernation section re-runs this card restore on a
       zero-length blob and ejects the card the Periph pass restored. Forwarded
       from the slot's owning Peripheral. */
    void SaveSlotState(StateWriter& w);
    void RestoreSlotState(StateReader& r);

    /* HostWidget. */
    std::wstring WidgetName() const override { return label_; }
    WidgetGroup  Group() const override { return WidgetGroup::Pcmcia; }
    std::wstring Tooltip() const override;
    std::vector<WidgetMenuItem> BuildMenu() override;
    void DrawIcon(HDC dc, const RECT& box) const override;
    bool PollDirty() override;

private:
    void InsertLocked(std::unique_ptr<PcmciaCard> card);
    void EjectLocked();

    /* Menu actions; gen guards against stale popup callbacks firing
       after the slot's contents changed. */
    void MenuEject(uint64_t gen);
    void CombinedSwap(uint64_t gen, std::unique_ptr<PcmciaCard> card);
    void MenuInsert(uint64_t gen, const std::string& card_id);
    /* Insert a pre-built card (gen-guarded) - used by card kinds whose
       insert menu resolves a host resource (e.g. CompactFlash image). */
    void MenuInsertCard(uint64_t gen, std::unique_ptr<PcmciaCard> card);
    std::vector<WidgetMenuItem> BuildInsertSubmenuLocked(uint64_t gen);
    WidgetMenuItem GuardCardItemLocked(WidgetMenuItem item, uint64_t gen);

    CerfEmulator&   emu_;
    PcmciaSlotHost& host_;
    std::wstring    label_;

    mutable std::mutex          bus_mutex_;
    std::unique_ptr<PcmciaCard> card_;
    bool                        powered_ = false;
    uint64_t                    generation_ = 0;

    std::wstring ui_last_res_;   /* last-drawn icon resource; UI thread only (PollDirty) */
};
