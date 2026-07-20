#include "host_widget_registry.h"

#include "../core/cerf_emulator.h"
#include "../state/state_stream.h"

#include <algorithm>
#include <string>

REGISTER_SERVICE(HostWidgetRegistry);

void HostWidgetRegistry::Register(HostWidget* w) {
    std::lock_guard<std::mutex> lk(mtx_);
    widgets_.push_back(w);
}

std::vector<HostWidget*> HostWidgetRegistry::Ordered() {
    std::vector<HostWidget*> v;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        v = widgets_;
    }
    std::stable_sort(v.begin(), v.end(), [](HostWidget* a, HostWidget* b) {
        const int ga = static_cast<int>(a->Group());
        const int gb = static_cast<int>(b->Group());
        if (ga != gb) return ga < gb;
        return a->WidgetName() < b->WidgetName();
    });
    return v;
}

void HostWidgetRegistry::ResetIds() {
    id_callbacks_.clear();
}

void HostWidgetRegistry::AppendItems(HMENU menu,
                                     const std::vector<WidgetMenuItem>& items) {
    for (const auto& it : items) {
        if (it.label.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        if (!it.submenu.empty()) {
            HMENU sub = CreatePopupMenu();
            AppendItems(sub, it.submenu);
            AppendMenuW(menu, MF_POPUP | (it.enabled ? 0u : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(sub), it.label.c_str());
            continue;
        }
        UINT flags = MF_STRING;
        if (!it.enabled || !it.on_click) flags |= MF_GRAYED;
        if (it.checked)                  flags |= MF_CHECKED;
        UINT id = 0;
        if (it.on_click && it.enabled) {
            id = static_cast<UINT>(kIdBase + id_callbacks_.size());
            id_callbacks_.push_back(it.on_click);
        }
        AppendMenuW(menu, flags, id, it.label.c_str());
    }
}

HMENU HostWidgetRegistry::BuildContextMenu(HostWidget* w) {
    ResetIds();
    HMENU m = CreatePopupMenu();
    AppendItems(m, w->BuildMenu());
    return m;
}

void HostWidgetRegistry::AppendAllToMenu(HMENU dest) {
    ResetIds();
    /* Reversed (Ordered() is ascending by Group): the highest-rank terminal
       widget - the capture lock - leads the block, the rest follow. One
       contiguous block, no inter-widget separators. */
    auto ordered = Ordered();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
        HostWidget* w = *it;
        auto items = w->BuildMenu();
        if (items.empty()) {
            /* No menu of its own - a disabled header keeps it discoverable. */
            AppendMenuW(dest, MF_STRING | MF_GRAYED, 0, w->WidgetName().c_str());
        } else if (items.size() == 1 && items[0].submenu.empty()) {
            AppendItems(dest, items);
        } else {
            HMENU sub = CreatePopupMenu();
            AppendItems(sub, items);
            AppendMenuW(dest, MF_POPUP, reinterpret_cast<UINT_PTR>(sub),
                        w->WidgetName().c_str());
        }
    }
}

void HostWidgetRegistry::Dispatch(int id) {
    const size_t idx = static_cast<size_t>(id - kIdBase);
    std::function<void()> cb;
    if (idx < id_callbacks_.size()) cb = id_callbacks_[idx];
    if (cb) cb();
}

void HostWidgetRegistry::SaveState(StateWriter& w) {
    auto ordered = Ordered();
    w.Write<uint32_t>(static_cast<uint32_t>(ordered.size()));
    for (HostWidget* widget : ordered) {
        const std::wstring name = widget->WidgetName();
        w.Write<uint32_t>(static_cast<uint32_t>(name.size()));
        w.WriteBytes(name.data(), name.size() * sizeof(wchar_t));
        const uint64_t len_off  = w.BytesWritten();
        w.Write<uint64_t>(0);                       /* blob-length placeholder. */
        const uint64_t body_off = w.BytesWritten();
        widget->SaveWidgetState(w);
        const uint64_t len = w.BytesWritten() - body_off;
        w.PatchAt(len_off, &len, sizeof(len));
    }
}

void HostWidgetRegistry::RestoreState(StateReader& r) {
    auto ordered = Ordered();
    uint32_t n = 0;
    r.Read(n);
    for (uint32_t i = 0; i < n && r.Ok(); ++i) {
        uint32_t namelen = 0;
        r.Read(namelen);
        if (!r.Ok() || namelen > 1024u) break;      /* corrupt; outer section frame realigns. */
        std::wstring name(namelen, L'\0');
        r.ReadBytes(name.data(), namelen * sizeof(wchar_t));
        uint64_t blob_len = 0;
        r.Read(blob_len);
        const uint64_t body_start = r.Position();
        HostWidget* target = nullptr;
        for (HostWidget* widget : ordered)
            if (widget->WidgetName() == name) { target = widget; break; }
        if (target) target->RestoreWidgetState(r);
        r.SeekTo(body_start + blob_len);            /* skip a missing/asymmetric widget. */
    }
}
