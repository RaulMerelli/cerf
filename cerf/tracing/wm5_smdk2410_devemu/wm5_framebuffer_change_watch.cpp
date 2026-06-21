#include "../trace_manager.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "wm5_bundle.h"

namespace {

constexpr uint32_t kFbPa       = 0x33F00000u;
constexpr uint32_t kFbWidth    = 240u;
constexpr uint32_t kFbHeight   = 320u;
constexpr uint32_t kFbBpp      = 2u;
constexpr uint32_t kFbBytes    = kFbWidth * kFbHeight * kFbBpp;

/* 16 well-distributed sample offsets covering top/middle/bottom of
   the framebuffer. Lined up to 4-byte boundaries so ReadWord is
   aligned. */
constexpr uint32_t kSampleOffsets[16] = {
    0x00000u, 0x00400u, 0x00800u, 0x01000u,
    0x04000u, 0x07000u, 0x0A000u, 0x0D000u,
    0x10000u, 0x12C00u, 0x14000u, 0x16800u,
    0x18000u, 0x1A400u, 0x1C800u, 0x250FCu,   /* last one near FB-end */
};

class TraceWm5FbChangeWatch : public Service {
public:
    using Service::Service;
    void OnReady() override {
        auto& tm = emu_.Get<TraceManager>();
        tm.RegisterForBundle(kWm5BundleCrc32, [&] {
            auto state = std::make_shared<State>();

            auto pc_logger = [](const char* tag) {
                return [tag](const TraceContext& c) {
                    /* Read the instruction at PC and the call-site BL
                       at LR-4 - together they identify what helper is
                       running and which call site invoked it, without
                       needing IDA pre-loaded for the owning module. */
                    auto insn_at_pc = c.ReadVa32(c.pc);
                    auto bl_at_lr   = c.ReadVa32(c.regs[14] - 4u);
                    LOG(Trace,
                        "[FB_PC_%s] PC=0x%08X insn=0x%08X "
                        "R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X "
                        "LR=0x%08X (BL@LR-4=0x%08X) SP=0x%08X "
                        "CPSR=0x%08X\n",
                        tag, c.pc,
                        insn_at_pc ? *insn_at_pc : 0xDEADBEEFu,
                        c.regs[0], c.regs[1], c.regs[2], c.regs[3],
                        c.regs[14],
                        bl_at_lr   ? *bl_at_lr   : 0xDEADBEEFu,
                        c.regs[13], c.cpsr);
                };
            };
            tm.OnPc(0x017C2098u, pc_logger("WRITER_A"));   /* red boot splash */
            tm.OnPc(0x017C4BACu, pc_logger("WRITER_B"));   /* black clear */
            tm.OnPc(0x017C4C1Cu, pc_logger("WRITER_C"));   /* WM logo paint */

            tm.OnRunLoopIter([state](const TraceContext& c) {
                auto& mem = c.emu.Get<EmulatedMemory>();
                uint32_t hash = 0u;
                uint32_t first_word = 0u;
                for (size_t i = 0; i < std::size(kSampleOffsets); ++i) {
                    const uint32_t pa = kFbPa + kSampleOffsets[i];
                    if (pa + 4u > kFbPa + kFbBytes) continue;
                    const uint32_t w = mem.ReadWord(pa);
                    if (i == 0) first_word = w;
                    /* Rotate-XOR so two equal words at different
                       positions don't cancel - captures position-
                       sensitive change. */
                    hash ^= ((w << (i & 31)) | (w >> ((32 - (i & 31)) & 31)));
                }

                ++state->total_polls;

                if (!state->initialized) {
                    state->initialized = true;
                    state->last_hash   = hash;
                    state->first_word_at_first_poll = first_word;
                    LOG(Trace, "[FB_WATCH] initial hash=0x%08X first_word=0x%08X "
                               "(poll #%llu)\n",
                        hash, first_word,
                        (unsigned long long)state->total_polls);
                    return;
                }

                if (hash != state->last_hash) {
                    ++state->changes;
                    LOG(Trace, "[FB_WATCH] change #%llu hash=0x%08X "
                               "(prev=0x%08X) first_word=0x%08X poll=#%llu "
                               "PC=0x%08X\n",
                        (unsigned long long)state->changes,
                        hash, state->last_hash, first_word,
                        (unsigned long long)state->total_polls,
                        c.pc);
                    state->last_hash = hash;
                }

                /* Heartbeat every N polls so even with zero changes
                   we know the trace is alive and counts are growing. */
                if ((state->total_polls % 100000ULL) == 0ULL) {
                    LOG(Trace, "[FB_WATCH] heartbeat poll=#%llu changes=%llu "
                               "current_hash=0x%08X\n",
                        (unsigned long long)state->total_polls,
                        (unsigned long long)state->changes,
                        hash);
                }
            });
        });
    }
private:
    struct State {
        bool         initialized                = false;
        uint32_t     last_hash                  = 0u;
        uint32_t     first_word_at_first_poll   = 0u;
        uint64_t     total_polls                = 0ULL;
        uint64_t     changes                    = 0ULL;
    };
};

REGISTER_SERVICE(TraceWm5FbChangeWatch);

}  /* namespace */
