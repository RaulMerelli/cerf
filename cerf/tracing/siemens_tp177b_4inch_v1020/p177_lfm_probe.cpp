#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* P177 TFFS3 LFM probe (dev diagnostic): logs the LFM device count (0x1F18734),
   range count (0x1F18744), and range-table ptr (0x1F18740) at LFM_FlashGetInfo
   entry (TFFS3.dll sub_34CF408), whose size-0 return aborts the VLBD mount at
   vlbd.cpp:675. Each 12-byte range: u16 clientId @+0, u8 deviceIdx @+2, u32 size @+8. */

namespace {

class TraceSiemensP177LfmProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kSiemensTp177bBundleCrc32, [this, &tm] {
            /* TFFS3.dll is loaded only by filesys.exe (the FSD host), so this
               user-mode VA has no cross-process alias - the unfiltered OnPc
               fires solely in filesys.exe and needs no process predicate. */
            tm.OnPc(0x34CF408u, [this](const TraceContext& c) {
                if (fired_ >= 8u) return;
                ++fired_;

                auto rd = [&](uint32_t va) -> long {
                    auto v = c.ReadVa32(va);
                    return v ? (long)*v : -1;
                };
                const long dev = rd(0x1F18734u);   /* device count  */
                const long rng = rd(0x1F18744u);   /* range count   */
                const long tbl = rd(0x1F18740u);   /* range tbl ptr */

                LOG(Trace, "[P177LFM] GetInfo client=0x%08X devCount=%ld "
                           "rangeCount=%ld rangeTbl=0x%08lX\n",
                    c.regs[0], dev, rng, tbl);

                /* If ranges exist, dump the first few range clientIds so a
                   clientId mismatch is visible against the requesting client. */
                if (rng > 0 && tbl > 0) {
                    const long n = rng < 6 ? rng : 6;
                    for (long i = 0; i < n; ++i) {
                        auto cid = c.ReadVa32((uint32_t)tbl + 12u * (uint32_t)i);
                        auto sz  = c.ReadVa32((uint32_t)tbl + 12u * (uint32_t)i + 8u);
                        auto f4  = c.ReadVa32((uint32_t)tbl + 12u * (uint32_t)i + 4u);
                        LOG(Trace, "[P177LFM]   range[%ld] clientId=0x%04X "
                                   "devIdx=%ld addr+4=0x%08lX size=%ld\n",
                            i, cid ? (*cid & 0xFFFFu) : 0xFFFFu,
                            cid ? (long)((*cid >> 16) & 0xFFu) : -1,
                            f4 ? (unsigned long)*f4 : 0,
                            sz ? (long)*sz : -1);
                    }
                }

                /* device[+32] is the MTD scan fn that builds the range array. */
                const long devArr = rd(0x1F18730u);
                if (dev > 0 && devArr > 0) {
                    const long nd = dev < 4 ? dev : 4;
                    for (long d = 0; d < nd; ++d) {
                        const uint32_t db = (uint32_t)devArr + 60u * (uint32_t)d;
                        LOG(Trace, "[P177LFM]   dev[%ld]@0x%08X rangeArr=0x%08lX "
                                   "rangeCnt=%ld sectorSize=%ld field20=%ld\n",
                            d, db, (unsigned long)rd(db + 8u),
                            (long)rd(db + 12u), (long)rd(db + 16u),
                            (long)rd(db + 20u));
                        LOG(Trace, "[P177LFM]   dev[%ld] fns +32=0x%08lX +36=0x%08lX "
                                   "+40=0x%08lX +44=0x%08lX +48=0x%08lX +52=0x%08lX\n",
                            d, (unsigned long)rd(db + 32u),
                            (unsigned long)rd(db + 36u), (unsigned long)rd(db + 40u),
                            (unsigned long)rd(db + 44u), (unsigned long)rd(db + 48u),
                            (unsigned long)rd(db + 52u));
                    }
                }
            });
        });
    }

private:
    uint32_t fired_ = 0;
};

REGISTER_SERVICE(TraceSiemensP177LfmProbe);

}  /* namespace */
