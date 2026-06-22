#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm/arm_mmu.h"
#include "devemu_wm653_bundle.h"

#include <set>
#include <string>

namespace {

/* cerf_guest offsets are stable while its SOURCE is unchanged (only cerf.exe
   relinks for a trace edit). Re-derive after a cerf_guest rebuild. */
constexpr uint32_t kOurVbase     = 0x7B590000u;
constexpr uint32_t kModHdrPa     = 0x03A5A000u;   /* injector log: mod_hdr pa */
constexpr uint32_t kDrvEnable    = 0x7B59447Cu;   /* DrvEnableDriver: gwes, BEFORE the carrier starts */
constexpr uint32_t kCddInit      = 0x7B598998u;   /* CDD_Init: the device.exe carrier context */
constexpr uint32_t kStructGlobal = 0x7B5C2998u;   /* a .data global (rva 0x32998) */

/* Real gwes data-abort PCs (UART): deref of a slot-6 ptr, FSR=0x407. */
constexpr uint32_t kGwesFaultPc[] = { 0x7B597758u, 0x7B5979CCu, 0x7B598404u };

/* cerf_guest .data span = vbase + (.data rva 0x23000, vsize 0x123B8). */
constexpr uint32_t kDataLo = 0x7B5B3000u;
constexpr uint32_t kDataHi = 0x7B5C6000u;

class TraceWm653ImgfsLoad : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kDevemuWm653BundleCrc32, [this, &tm] {

            tm.OnRunLoopIter([this](const TraceContext& c) {
                if (hdr_done_) return;
                hdr_done_ = true;
                const uint32_t vb =
                    c.emu.Get<EmulatedMemory>().ReadWord(kModHdrPa + 0x08u);
                LOG(Trace, "[WM653_LOAD] injected mod_hdr e32_vbase=0x%08X (expect "
                           "0x%08X) %s\n", vb, kOurVbase,
                    vb == kOurVbase ? "PERSISTED" : "LOST");
            });

            /* Same globHost across these three contexts => shared .data; slot6 count
               pre (DrvEnable) vs post (GwesFault) => when the poisoning happened. */
            tm.OnPc(kDrvEnable, [this](const TraceContext& c) { Snap(c, "DrvEnable"); });
            tm.OnPc(kCddInit,   [this](const TraceContext& c) { Snap(c, "CddInit"); });
            for (uint32_t pc : kGwesFaultPc)
                tm.OnPc(pc, [this](const TraceContext& c) { Snap(c, "GwesFault"); });
        });
    }

private:
    void Snap(const TraceContext& c, const char* where) {
        if (done_.count(where)) return;
        done_.insert(where);
        auto& mmu = c.emu.Get<ArmMmu>();
        const uint32_t pid = mmu.State()->process_id;
        uint8_t* gh = mmu.PeekVaToHost(kStructGlobal);
        uint32_t found = 0, first[4] = {0, 0, 0, 0};
        for (uint32_t va = kDataLo; va < kDataHi; va += 0x1000u) {
            uint8_t* h = mmu.PeekVaToHost(va);
            if (!h) continue;
            const uint32_t* w = reinterpret_cast<const uint32_t*>(h);
            for (uint32_t i = 0; i < 1024; ++i)
                if (w[i] >= 0x0C000000u && w[i] < 0x0E000000u) {
                    if (found < 4) first[found] = va + i * 4u;
                    ++found;
                }
        }
        LOG(Trace, "[WM653_SNAP] %-9s pid=0x%08X globHost=%p slot6Words=%u "
                   "first@[%08X %08X %08X %08X]\n",
            where, pid, (void*)gh, found, first[0], first[1], first[2], first[3]);
    }

    bool                                 hdr_done_ = false;
    std::set<std::string>                done_;
};

REGISTER_SERVICE(TraceWm653ImgfsLoad);

}  /* namespace */
