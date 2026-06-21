#pragma once

#include "../../host/host_widget.h"

#include <cstdint>
#include <string>
#include <vector>

class CerfEmulator;
class PcmciaSlot;
class StateWriter;
class StateReader;

/* One emulated 16-bit PC Card. Instances live from insert to eject and
   one card type may occupy two slots at once - multi-instance by
   contract. Attribute memory is valid on even bytes only; the socket
   controller synthesizes halfword attribute reads. */
class PcmciaCard {
public:
    explicit PcmciaCard(CerfEmulator& emu) : emu_(emu) {}
    virtual ~PcmciaCard() = default;

    virtual std::wstring DisplayName() const = 0;

    /* Stable hibernation type tag: the slot writes it so a saved card's
       register state is applied only when the same card type is present. */
    virtual const char* SaveId() const = 0;
    /* Guest-visible register state. The slot length-frames this body so a
       non-matching live card can be skipped on restore. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    /* Host binding needed to rebuild this card on restore (e.g. CF image
       path, serial COM name). Empty for cards rebuildable from id alone. */
    virtual std::wstring SaveBinding() const { return {}; }

    /* Status-bar tooltip detail (e.g. name + MAC). */
    virtual std::wstring TooltipDetail() const { return DisplayName(); }

    /* Status-bar icon: a cerf.rc ICON resource name. Defaults to the generic
       inserted-card icon; specific card types override. */
    virtual const wchar_t* IconResource() const { return L"ICON_PCMCIA_CARD"; }

    /* Socket Vcc edges, driven by the slot when the controller's power
       register flips. */
    virtual void PowerOn()  = 0;
    virtual void PowerOff() = 0;

    /* Socket RESET line pulse (controller reset bit released while the
       socket is powered). Power-cycle is the faithful default: the pin
       returns the card to its power-on state. */
    virtual void SocketReset() { PowerOff(); PowerOn(); }

    virtual uint8_t  ReadAttribute8 (uint32_t offset)                = 0;
    virtual void     WriteAttribute8(uint32_t offset, uint8_t value) = 0;

    virtual uint8_t  ReadCommon8  (uint32_t offset)                  = 0;
    virtual uint16_t ReadCommon16 (uint32_t offset)                  = 0;
    virtual void     WriteCommon8 (uint32_t offset, uint8_t  value)  = 0;
    virtual void     WriteCommon16(uint32_t offset, uint16_t value)  = 0;

    virtual uint8_t  ReadIo8  (uint32_t offset)                      = 0;
    virtual uint16_t ReadIo16 (uint32_t offset)                      = 0;
    virtual void     WriteIo8 (uint32_t offset, uint8_t  value)      = 0;
    virtual void     WriteIo16(uint32_t offset, uint16_t value)      = 0;

    /* Called by the slot right after AttachSlot, under the slot's bus
       lock. The card hooks up its host-side transports here (e.g. the
       NE2000 installs its NetworkBackend RX callback); slot_ is
       guaranteed valid from this point until destruction. */
    virtual void OnInserted() {}

    /* Slot forwards this during the quiesce phase, peers still alive. Detach
       from peers here (e.g. clear the NetworkBackend RX callback): a peer
       readied in this card's ctor is destroyed first, so a detach in ~card
       calls through freed memory. */
    virtual void OnShutdown() {}

    /* Card-defined extra menu, appended below a separator at the bottom
       of the owning slot's menu. Empty = no card section. Item
       callbacks run under the slot's bus lock and must NOT call the
       slot's InsertCard/EjectCard (self-deadlock). */
    virtual std::vector<WidgetMenuItem> BuildCardMenu() { return {}; }

    /* Wired by PcmciaSlot::InsertCard before OnInserted. */
    void AttachSlot(PcmciaSlot* slot) { slot_ = slot; }

protected:
    CerfEmulator& emu_;
    PcmciaSlot*   slot_ = nullptr;
};
