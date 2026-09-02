#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../core/log.h"
#include "imx6_vivante_blit.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_state.h"

namespace imx6_vivante {

/* Vivante GC front-end command-stream interpreter.  Command acquisition follows
   rnndb exactly: FE.COMMAND_ADDRESS is physical, while LINK/CALL targets are
   VIVM addresses translated by the FE MMU. */
class VivanteFe {
public:
    VivanteFe(VivanteState& s, VivanteMem& mem, VivanteBlit& blit) : s_(s), mem_(mem), blit_(blit) {}

    void AdvanceFrontendRing() {
        if (!s_.fe_live_ || s_.fe_ring_pc_ == 0u || s_.fe_in_advance_) return;
        s_.fe_in_advance_ = true;

        if (s_.fe_idle_ring_) {
            uint32_t w0 = 0u;
            mem_.ReadCommandWords(s_.fe_ring_pc_, &w0, 1u, s_.fe_address_space_);
            const uint32_t op = w0 >> 27;
            if (op == kFeLink) {
                s_.fe_idle_ring_ = false;
                s_.fe_window_words_ = FePrefetchDwords(s_.fe_ring_prefetch_);
            } else if (op != kFeWait) {
                s_.fe_in_advance_ = false;
                return;
            }
        }

        if (s_.fe_idle_ring_) {
            IdleRingInfo idle;
            if (mem_.DetectIdleRing(s_.fe_ring_pc_, s_.fe_address_space_, idle)) {
                s_.fe_ring_pc_ = idle.target;
                s_.fe_address_space_ = idle.address_space;
                s_.regs_[0x664u >> 2] = s_.fe_ring_pc_;
            }
            s_.fe_in_advance_ = false;
            return;
        }

        IdleRingInfo idle;
        if (mem_.DetectIdleRing(s_.fe_ring_pc_, s_.fe_address_space_, idle)) {
            s_.fe_ring_pc_ = idle.target;
            s_.fe_address_space_ = idle.address_space;
            s_.fe_idle_ring_ = true;
            s_.regs_[0x664u >> 2] = s_.fe_ring_pc_;
            s_.fe_in_advance_ = false;
            return;
        }

        FeStats stats;
        const uint32_t start_pc = s_.fe_ring_pc_;
        const uint32_t final_pc =
            ExecuteCommandStream(start_pc, s_.fe_ring_prefetch_, stats, true, s_.fe_address_space_);
        s_.regs_[0x664u >> 2] = final_pc;

        if (stats.blocked) {
            s_.fe_ring_pc_ = final_pc;
            s_.fe_live_ = true;
            s_.fe_idle_ring_ = false;
        } else if (s_.fe_resume_idle_target_ != 0u) {
            s_.fe_ring_pc_ = s_.fe_resume_idle_target_;
            s_.fe_address_space_ = s_.fe_resume_address_space_;
            s_.fe_resume_idle_target_ = 0u;
            s_.fe_ring_prefetch_ = 2u;
            s_.fe_window_words_ = 0u;
            s_.fe_call_depth_ = 0u;
            s_.fe_live_ = true;
            s_.fe_idle_ring_ = true;
            s_.regs_[0x664u >> 2] = s_.fe_ring_pc_;
        } else if (stats.idle_ring) {
            s_.fe_ring_pc_ = final_pc ? final_pc : start_pc;
            s_.fe_ring_prefetch_ = 2u;
            s_.fe_idle_ring_ = true;
        } else if (stats.stopped) {
            s_.fe_live_ = false;
            s_.fe_idle_ring_ = false;
        }
        s_.fe_in_advance_ = false;
    }

    void RunFrontend(uint32_t control) {
        const uint32_t prefetch = control & kFeCommandPrefetchMask;
        const uint32_t address = s_.regs_[0x654u >> 2];
        constexpr FeCommandAddressSpace kBootstrapSpace = FeCommandAddressSpace::Physical;
        mem_.DumpCommandWords(address, prefetch, kBootstrapSpace);

        IdleRingInfo idle;
        if (prefetch >= 2u && mem_.DetectIdleRing(address, kBootstrapSpace, idle)) {
            s_.regs_[0x664u >> 2] = idle.target;
            s_.fe_live_ = true;
            s_.fe_ring_pc_ = idle.target;
            s_.fe_address_space_ = idle.address_space;
            s_.fe_ring_prefetch_ = 2u;
            s_.fe_window_words_ = 0u;
            s_.fe_idle_ring_ = true;
            return;
        }

        s_.fe_live_ = true;
        s_.fe_idle_ring_ = false;
        FeStats stats;
        const uint32_t final_pc = ExecuteCommandStream(address, prefetch, stats, false, kBootstrapSpace);
        s_.regs_[0x664u >> 2] = final_pc;

        if (stats.blocked) {
            s_.fe_live_ = true;
            s_.fe_ring_pc_ = final_pc;
            s_.fe_ring_prefetch_ = 0u;
            s_.fe_idle_ring_ = false;
        } else if (stats.idle_ring) {
            s_.fe_live_ = true;
            s_.fe_ring_pc_ = final_pc ? final_pc : address;
            s_.fe_ring_prefetch_ = 2u;
            s_.fe_idle_ring_ = true;
        } else if (stats.stopped) {
            s_.fe_live_ = false;
            s_.fe_idle_ring_ = false;
        } else {
            s_.fe_idle_ring_ = false;
        }
    }

    bool CommandFits(uint32_t command_words, uint32_t window_words, uint32_t pc, FeStats& stats) {
        if (command_words <= window_words) return true;
        stats.blocked = true;
        s_.fe_window_words_ = window_words;
        s_.regs_[0x664u >> 2] = pc;
        return false;
    }

    bool FetchWords(uint32_t pc, uint32_t* words, uint32_t count, FeCommandAddressSpace address_space,
                    uint32_t window_words, FeStats& stats) {
        if (mem_.ReadCommandWords(pc, words, count, address_space)) return true;
        stats.blocked = true;
        s_.fe_window_words_ = window_words;
        s_.regs_[0x664u >> 2] = pc;
        return false;
    }

    void ConsumeWords(uint32_t words, uint32_t& pc, uint32_t& window_words) const {
        pc += words * 4u;
        window_words -= words;
    }

    uint32_t ExecuteCommandStream(uint32_t address, uint32_t prefetch, FeStats& stats, bool resume,
                                  FeCommandAddressSpace start_space) {
        uint32_t pc = address;
        uint32_t window_words = resume ? s_.fe_window_words_ : FePrefetchDwords(prefetch);
        FeCommandAddressSpace address_space = resume ? s_.fe_address_space_ : start_space;
        if (!resume) {
            s_.fe_call_depth_ = 0u;
            s_.fe_resume_idle_target_ = 0u;
        }

        auto finish = [&](uint32_t final_pc) {
            s_.fe_address_space_ = address_space;
            return final_pc;
        };

        for (uint32_t step = 0; step < 512u; ++step) {
            if (window_words == 0u) {
                /* cmdstream.xml requires the fetched range to terminate with
                   END/LINK/CALL/RETURN.  Exhaustion without control transfer
                   starves the decoder; it does not complete successfully. */
                stats.blocked = true;
                s_.fe_window_words_ = 0u;
                return finish(pc);
            }
            if (!CommandFits(2u, window_words, pc, stats)) return finish(pc);

            s_.regs_[0x664u >> 2] = pc;

            uint32_t words[4]{};
            if (!FetchWords(pc, words, 2u, address_space, window_words, stats)) {
                return finish(pc);
            }

            const uint32_t cmd = words[0];
            const uint32_t op = cmd >> 27;

            if ((op == kFeWait && window_words >= 4u) || op == kFeLink) {
                IdleRingInfo idle;
                if (mem_.DetectIdleRing(pc, address_space, idle)) {
                    stats.idle_ring = true;
                    s_.fe_window_words_ = 0u;
                    s_.fe_call_depth_ = 0u;
                    address_space = idle.address_space;
                    return finish(idle.target);
                }
            }

            ++stats.commands;

            switch (op) {
            case kFeLoadState: {
                uint32_t count = (cmd >> 16) & 0x3FFu;
                if (count == 0u) count = 1024u;
                const uint32_t state_word = cmd & 0xFFFFu;
                const bool fixp = (cmd & (1u << 26)) != 0u;
                const uint32_t len = (1u + count + 1u) & ~1u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                std::vector<uint32_t> packet(len);
                if (!FetchWords(pc, packet.data(), len, address_space, window_words, stats)) {
                    return finish(pc);
                }
                ApplyLoadState(packet.data() + 1u, state_word, count, fixp, stats);
                ++stats.load_state;
                ConsumeWords(len, pc, window_words);
                break;
            }
            case kFeEnd:
                if (cmd & 0x00000100u) {
                    const uint32_t event_id = cmd & 0x1Fu;
                    mem_.RaiseInterrupt(1u << event_id);
                    ++stats.events;
                }
                stats.stopped = true;
                s_.fe_window_words_ = 0u;
                s_.fe_call_depth_ = 0u;
                return finish(pc + 4u);

            case kFeNop:
            case kFeWait:
            case kFeChipSelect:
            case kFeSnapPages: {
                const uint32_t len = FeAlignedCommandWords(1u);
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                if (op == kFeWait) ++stats.waits;
                if (op == kFeChipSelect) s_.chip_select_mask_ = cmd & 0xFFFFu;
                ConsumeWords(len, pc, window_words);
                break;
            }

            case kFeDraw2d: {
                if (!mem_.Is2d()) mem_.HaltUnsupported("imx6-vivante DRAW_2D on non-GC320 core", pc, cmd);
                const uint32_t rects = ((cmd >> 8) & 0xFFu) ? ((cmd >> 8) & 0xFFu) : 256u;
                const uint32_t data_count = (cmd >> 16) & 0x7FFu;
                const uint32_t len = FeDraw2dPacketWords(rects, data_count);
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);

                std::vector<uint32_t> packet(len);
                if (!FetchWords(pc, packet.data(), len, address_space, window_words, stats)) {
                    return finish(pc);
                }
                ++stats.draw_2d;
                blit_.ExecuteDraw2d(packet.data() + 2u, rects, packet.data() + 2u + rects * 2u, data_count);
                ConsumeWords(len, pc, window_words);
                break;
            }

            case kFeDrawPrimitives: {
                constexpr uint32_t len = 4u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                ++stats.draw_3d;
                ConsumeWords(len, pc, window_words);
                break;
            }
            case kFeDrawIndexedPrimitives: {
                constexpr uint32_t len = 6u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                ++stats.draw_3d;
                ConsumeWords(len, pc, window_words);
                break;
            }

            case kFeLink: {
                constexpr uint32_t len = 2u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                const uint32_t link_prefetch = cmd & 0xFFFFu;
                const uint32_t link_address = words[1];
                ++stats.links;
                pc = link_address;
                window_words = FePrefetchDwords(link_prefetch);
                address_space = FeCommandAddressSpace::Virtual;
                break;
            }

            case kFeStall: {
                constexpr uint32_t len = 2u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                const uint32_t token = words[1];
                if (!mem_.TryConsumeSemaphoreToken(token)) {
                    stats.blocked = true;
                    s_.fe_window_words_ = window_words;
                    return finish(pc);
                }
                ConsumeWords(len, pc, window_words);
                break;
            }

            case kFeWaitFence: {
                constexpr uint32_t len = 2u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                const uint32_t fence_address = words[1];
                uint64_t actual = 0u;
                if (!mem_.ReadMemoryU64(fence_address, actual))
                    mem_.HaltUnsupported("imx6-vivante WAIT_FENCE unreadable address", fence_address, cmd);
                const uint64_t expected = static_cast<uint64_t>(mem_.StateReg(0x007E8u)) |
                                          (static_cast<uint64_t>(mem_.StateReg(0x007F4u)) << 32);
                if (actual != expected) {
                    stats.blocked = true;
                    s_.fe_window_words_ = window_words;
                    return finish(pc);
                }
                ConsumeWords(len, pc, window_words);
                break;
            }

            case kFeCall: {
                constexpr uint32_t len = 4u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                if (!FetchWords(pc, words, len, address_space, window_words, stats)) {
                    return finish(pc);
                }
                if (s_.fe_call_depth_ >= kFeCallStackDepth)
                    mem_.HaltUnsupported("imx6-vivante FE CALL stack overflow", pc, cmd);
                s_.fe_call_stack_[s_.fe_call_depth_++] = {
                    words[3],
                    FePrefetchDwords(words[2] & 0xFFFFu),
                    address_space,
                };
                pc = words[1];
                window_words = FePrefetchDwords(cmd & 0xFFFFu);
                address_space = FeCommandAddressSpace::Virtual;
                break;
            }

            case kFeReturn:
                if (s_.fe_call_depth_ == 0u) mem_.HaltUnsupported("imx6-vivante FE RETURN stack underflow", pc, cmd);
                {
                    const FeCallFrame frame = s_.fe_call_stack_[--s_.fe_call_depth_];
                    pc = frame.return_address;
                    window_words = frame.return_window_words;
                    address_space = frame.return_address_space;
                }
                break;

            case kFeDrawInstanced: {
                constexpr uint32_t len = 4u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                ++stats.draw_3d;
                ConsumeWords(len, pc, window_words);
                break;
            }
            case kFeDrawIndirect: {
                constexpr uint32_t len = 2u;
                if (!CommandFits(len, window_words, pc, stats)) return finish(pc);
                ++stats.draw_3d;
                ConsumeWords(len, pc, window_words);
                break;
            }
            default: mem_.HaltUnsupported("imx6-vivante FE unknown opcode", pc, cmd);
            }
        }

        stats.blocked = true;
        s_.fe_window_words_ = window_words;
        return finish(pc);
    }

    void ApplyLoadState(const uint32_t* values, uint32_t state_word, uint32_t count, bool fixp, FeStats& stats) {
        mem_.EnsureStateSize();
        bool vr_kick = false;
        uint32_t vr_start = 0u;
        bool rs_kick = false;
        bool rs_inplace_kick = false;
        uint32_t rs_inplace_tiles = 0u;
        uint32_t pending_event_bits = 0u;
        uint32_t pending_event_count = 0u;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t v = values[i];
            if (fixp) {
                const float converted = static_cast<float>(static_cast<int32_t>(v)) / 65536.0f;
                std::memcpy(&v, &converted, sizeof(v));
            }
            const uint32_t idx = state_word + i;
            if (idx < s_.state_.size()) {
                mem_.StoreStateReg(idx << 2, v);
                if ((idx << 2) == 0x01294u && (v & (1u << 3)) == 0u) {
                    vr_kick = true;
                    vr_start = v & 3u;
                }
                if ((idx << 2) == 0x01600u && v != 0u) rs_kick = true;
                if ((idx << 2) == 0x016B0u && v != 0u) {
                    rs_inplace_kick = true;
                    rs_inplace_tiles = v;
                }
                if ((idx << 2) == 0x03804u) { /* VIVS_GL_EVENT */
                    const uint32_t event_id = v & 0x1Fu;
                    const bool from_gpu_pipe = (v & 0xE0u) != 0u;
                    if (from_gpu_pipe && event_id < 30u) {
                        pending_event_bits |= 1u << event_id;
                        ++pending_event_count;
                    }
                }
            }
        }
        if (vr_kick) {
            blit_.ExecuteVideoRasterizer(vr_start);
        }
        if (rs_kick) {
            /* etnaviv rnndb places RS_KICKER at 0x1600, before RS_CONFIG and
               the source/destination/window registers.  GALCore can submit a
               contiguous LOAD_STATE block starting at RS_KICKER; real hardware
               observes the programmed state packet coherently, while executing
               the kick in the middle of our state-copy loop runs RS with stale
               config/addresses and leaves stale UI rectangles. */
            blit_.ExecuteRs();
        }
        if (rs_inplace_kick) blit_.ExecuteRsInPlace(rs_inplace_tiles);
        if (pending_event_bits != 0u) {
            /* etnaviv kernel_interface.md: event queues schedule SIGNAL/WRITE
               work after the GPU has finished the committed command buffer.
               LOAD_STATE is packet-atomic here: notify only after any kick
               triggered by the same packet has completed. */
            mem_.RaiseInterrupt(pending_event_bits);
            stats.events += pending_event_count;
        }
    }

private:
    VivanteState& s_;
    VivanteMem& mem_;
    VivanteBlit& blit_;
};

} // namespace imx6_vivante
