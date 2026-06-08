#include "pcmcia_slot.h"

#include "pcmcia_card_catalog.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"

#include <utility>

namespace {

/* PCMCIA bus convention: with no card (or no Vcc) nothing drives the
   data lines and reads float high. */
constexpr uint8_t  kFloat8  = 0xFFu;
constexpr uint16_t kFloat16 = 0xFFFFu;

}  /* namespace */

PcmciaSlot::PcmciaSlot(CerfEmulator& emu, PcmciaSlotHost& host,
                       std::wstring label)
    : emu_(emu), host_(host), label_(std::move(label)) {}

bool PcmciaSlot::HasCard() const {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    return card_ != nullptr;
}

bool PcmciaSlot::IsPowered() const {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    return powered_ && card_ != nullptr;
}

void PcmciaSlot::SetPowered(bool on) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (powered_ == on) return;
    powered_ = on;
    if (!card_) return;
    if (on) card_->PowerOn();
    else    card_->PowerOff();
}

void PcmciaSlot::ResetCard() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->SocketReset();
}

uint8_t PcmciaSlot::ReadAttribute8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return kFloat8;
    return card_->ReadAttribute8(offset);
}

void PcmciaSlot::WriteAttribute8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->WriteAttribute8(offset, value);
}

uint8_t PcmciaSlot::ReadCommon8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return kFloat8;
    return card_->ReadCommon8(offset);
}

uint16_t PcmciaSlot::ReadCommon16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return kFloat16;
    return card_->ReadCommon16(offset);
}

void PcmciaSlot::WriteCommon8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->WriteCommon8(offset, value);
}

void PcmciaSlot::WriteCommon16(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->WriteCommon16(offset, value);
}

uint8_t PcmciaSlot::ReadIo8(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return kFloat8;
    return card_->ReadIo8(offset);
}

uint16_t PcmciaSlot::ReadIo16(uint32_t offset) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return kFloat16;
    return card_->ReadIo16(offset);
}

void PcmciaSlot::WriteIo8(uint32_t offset, uint8_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->WriteIo8(offset, value);
}

void PcmciaSlot::WriteIo16(uint32_t offset, uint16_t value) {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_ || !powered_) return;
    card_->WriteIo16(offset, value);
}

void PcmciaSlot::RaiseIrq() { host_.OnCardIrqAsserted(*this); }
void PcmciaSlot::ClearIrq() { host_.OnCardIrqDeasserted(*this); }

void PcmciaSlot::InsertLocked(std::unique_ptr<PcmciaCard> card) {
    card_ = std::move(card);
    card_->AttachSlot(this);
    card_->OnInserted();
    if (powered_) card_->PowerOn();
    ++generation_;
    LOG(Pcmcia, "[Slot %ls] inserted: %ls\n", label_.c_str(),
        card_->DisplayName().c_str());
}

void PcmciaSlot::EjectLocked() {
    LOG(Pcmcia, "[Slot %ls] ejected: %ls\n", label_.c_str(),
        card_->DisplayName().c_str());
    if (powered_) card_->PowerOff();
    card_.reset();
    ++generation_;
}

void PcmciaSlot::InsertCard(std::unique_ptr<PcmciaCard> card) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (card_) {
            LOG(Caution, "[Slot %ls] InsertCard into occupied slot "
                    "(%ls) — rejected\n",
                label_.c_str(), card_->DisplayName().c_str());
            return;
        }
        InsertLocked(std::move(card));
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::OnShutdown() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (card_) card_->OnShutdown();
}

void PcmciaSlot::EjectCard() {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (!card_) return;
        EjectLocked();
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::MenuEject(uint64_t gen) {
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen || !card_) return;
        EjectLocked();
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::MenuInsert(uint64_t gen, const std::string& card_id) {
    bool ejected = false;
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen) return;
        if (card_) {
            EjectLocked();
            ejected = true;
        }
    }
    if (ejected) host_.OnCardDetectChanged(*this);

    auto card = emu_.Get<PcmciaCardCatalog>().Create(card_id);
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (card_) return;   /* raced with another inserter; keep theirs */
        InsertLocked(std::move(card));
    }
    host_.OnCardDetectChanged(*this);
}

void PcmciaSlot::MenuInsertCard(uint64_t gen, std::unique_ptr<PcmciaCard> card) {
    if (!card) return;
    bool ejected = false;
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (generation_ != gen) return;
        if (card_) { EjectLocked(); ejected = true; }
    }
    if (ejected) host_.OnCardDetectChanged(*this);
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        if (card_) return;   /* raced with another inserter; keep theirs */
        InsertLocked(std::move(card));
    }
    host_.OnCardDetectChanged(*this);
}

std::vector<WidgetMenuItem> PcmciaSlot::BuildInsertSubmenuLocked(uint64_t gen) {
    std::vector<WidgetMenuItem> items;
    for (const auto& e : emu_.Get<PcmciaCardCatalog>().Entries()) {
        WidgetMenuItem it;
        it.label = e.display_name;
        if (e.insert_submenu) {
            /* Card kind contributes its own insert submenu; the inserter
               places a card it built (e.g. from a file dialog). */
            auto inserter = [this, gen](std::unique_ptr<PcmciaCard> card) {
                MenuInsertCard(gen, std::move(card));
            };
            it.submenu = e.insert_submenu(inserter);
        } else {
            const std::string id = e.id;
            it.on_click = [this, gen, id] { MenuInsert(gen, id); };
        }
        items.push_back(std::move(it));
    }
    return items;
}

WidgetMenuItem PcmciaSlot::GuardCardItemLocked(WidgetMenuItem item,
                                               uint64_t gen) {
    if (item.on_click) {
        auto inner = std::move(item.on_click);
        item.on_click = [this, gen, inner] {
            std::lock_guard<std::mutex> lk(bus_mutex_);
            if (generation_ != gen) return;
            inner();
        };
    }
    for (auto& sub : item.submenu) {
        sub = GuardCardItemLocked(std::move(sub), gen);
    }
    return item;
}

std::vector<WidgetMenuItem> PcmciaSlot::BuildMenu() {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    const uint64_t gen = generation_;
    std::vector<WidgetMenuItem> items;

    if (!card_) {
        WidgetMenuItem insert;
        insert.label   = L"Insert card";
        insert.submenu = BuildInsertSubmenuLocked(gen);
        items.push_back(std::move(insert));
        return items;
    }

    WidgetMenuItem header;
    header.label   = card_->DisplayName();
    header.enabled = false;
    items.push_back(std::move(header));

    WidgetMenuItem eject;
    eject.label    = L"Eject";
    eject.on_click = [this, gen] { MenuEject(gen); };
    items.push_back(std::move(eject));

    WidgetMenuItem swap;
    swap.label   = L"Eject and insert";
    swap.submenu = BuildInsertSubmenuLocked(gen);
    items.push_back(std::move(swap));

    auto card_items = card_->BuildCardMenu();
    if (!card_items.empty()) {
        items.emplace_back();   /* separator */
        for (auto& ci : card_items) {
            items.push_back(GuardCardItemLocked(std::move(ci), gen));
        }
    }
    return items;
}

std::wstring PcmciaSlot::Tooltip() const {
    std::lock_guard<std::mutex> lk(bus_mutex_);
    if (!card_) return label_ + L" — empty";
    return label_ + L" — " + card_->TooltipDetail();
}

void PcmciaSlot::DrawIcon(HDC dc, const RECT& box) const {
    bool has;
    {
        std::lock_guard<std::mutex> lk(bus_mutex_);
        has = card_ != nullptr;
    }

    const int cx = (box.left + box.right) / 2;
    const int cy = (box.top + box.bottom) / 2;
    RECT body = { cx - 8, cy - 6, cx + 8, cy + 6 };

    HBRUSH  fill = CreateSolidBrush(has ? RGB(46, 58, 74) : RGB(28, 28, 30));
    HPEN    pen  = CreatePen(PS_SOLID, 1,
                             has ? RGB(160, 175, 195) : RGB(105, 105, 110));
    HGDIOBJ ob   = SelectObject(dc, fill);
    HGDIOBJ op   = SelectObject(dc, pen);
    Rectangle(dc, body.left, body.top, body.right, body.bottom);

    /* 68-pin connector strip on the left edge. */
    HBRUSH conn = CreateSolidBrush(has ? RGB(190, 175, 110)
                                       : RGB(90, 88, 70));
    RECT strip = { body.left + 1, body.top + 2,
                   body.left + 3, body.bottom - 2 };
    FillRect(dc, &strip, conn);
    DeleteObject(conn);

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(fill);
    DeleteObject(pen);
}

bool PcmciaSlot::PollDirty() {
    const bool has = HasCard();
    if (has == ui_last_has_card_) return false;
    ui_last_has_card_ = has;
    return true;
}
