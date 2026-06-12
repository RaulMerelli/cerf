#include "../trace_manager.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "bundle.h"

#include <cstdint>

/* Passive observation of keybddr.dll (NO injection): logs the VK keybddr emits to
   GWES (off_1BD6164 at 0x1BD482C/4914/498C: R0=keycode, R1=VK), to verify a REAL
   host keypress. pco/keybddr are XIP, each loaded by one process -> hooks
   unambiguous. */
namespace {

class TraceNecPcoInputProbe : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kNecMobilePro900Ce42BundleCrc32, [&tm] {
            tm.OnPc(0x1BD2D8Cu, [](const TraceContext&) { LOG(Trace, "[PCO-KBD] matrix scan\n"); });
            const TraceHandler vk = [](const TraceContext& c) {
                LOG(Trace, "[PCO-VK] keycode=0x%X vk=0x%02X\n", c.regs[0], c.regs[1] & 0xFFu);
            };
            tm.OnPc(0x1BD482Cu, vk);
            tm.OnPc(0x1BD4914u, vk);
            tm.OnPc(0x1BD498Cu, vk);
        });
    }
};

REGISTER_SERVICE(TraceNecPcoInputProbe);

}  /* namespace */
