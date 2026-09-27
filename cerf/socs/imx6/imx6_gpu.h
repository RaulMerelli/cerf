#pragma once

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../../state/emulation_freeze.h"
#include "imx6_vivante_state.h"
#include "imx6_mmio_lane.h"
#include "imx6_vivante_identity.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_blit.h"
#include "imx6_vivante_fe.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include "imx6_id.h"

namespace imx6_vivante {

/* Linux imx6qdl.dtsi defines GPU3D at 0x00130000/SPI9 and GPU2D at
   0x00134000/SPI10; IMX6DQRM Rev.2 Table 2-1 defines the apertures. */
template <uint32_t kBase, VivanteCore kCore, int kIrqSpi> class Imx6Gpu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx6;
    }
    void OnReady() override {
        mem_ = std::make_unique<VivanteMem>(st_, emu_, Core(), IrqSpi());
        blit_ = std::make_unique<VivanteBlit>(st_, *mem_);
        fe_ = std::make_unique<VivanteFe>(st_, *mem_, *blit_);
        emu_.Get<PeripheralDispatcher>().RegisterResettable(this);
        fe_worker_ = std::thread([this] { FeLoop(); });
    }
    void OnShutdown() override {
        fe_stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> g(fe_cv_mtx_);
            fe_cv_.notify_all();
        }
        if (fe_worker_.joinable()) fe_worker_.join();
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    VivanteCore Core() const { return kCore; }
    bool Is2d() const { return Core() == VivanteCore::Gc3202d; }
    int IrqSpi() const { return kIrqSpi; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t word_off = (addr - MmioBase()) & ~3u;
        if (word_off == 0x0DCu || word_off == 0x100u)
            HaltUnsupportedAccess("read8 Vivante strict probe", addr, 0u);
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t word_off = (addr - MmioBase()) & ~3u;
        if (word_off == 0x0DCu || word_off == 0x100u)
            HaltUnsupportedAccess("read16 Vivante strict probe", addr, 0u);
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        const uint32_t off = addr - MmioBase();
        const auto& identity = IdentityFor(Core());
        if (off == 0x000u || off == 0x004u || off == 0x010u || off == 0x664u || off == 0x668u || off == 0x66Cu) {
            fe_->AdvanceFrontendRing();
        }
        uint32_t value = 0;
        switch (off) {
        case 0x000: {
            value = (1u << 16) | (1u << 17) | (1u << 18);
            if (FrontendBusy()) {
                if (Core() == VivanteCore::Gc3202d)
                    value &= ~(1u << 17);
                else if (Core() == VivanteCore::Gc355Vg)
                    value &= ~(1u << 18);
                else
                    value &= ~(1u << 16);
            }
            break;
        }
        case 0x004: {
            value = 0x8007FFFFu;
            if (FrontendBusy()) {
                if (Core() == VivanteCore::Gc3202d) {
                    value &= ~((1u << 0) | (1u << 1) | (1u << 2));
                } else if (Core() == VivanteCore::Gc355Vg) {
                    value &= ~((1u << 0) | (1u << 8));
                } else {
                    value &= ~0xFFu;
                }
                value &= ~(1u << 31);
            }
            break;
        }
        case 0x008:  value = st_.regs_[off >> 2]; break;
        case 0x00c:  value = 0u; break;
        case 0x010:
            value = st_.intr_status_;
            if (st_.intr_status_ != 0u) {
                st_.intr_status_ = 0u;
                mem_->UpdateIrq();
            }
            break;
        case 0x014:  value = st_.intr_enable_; break;
        case 0x108:  value = 0u; break;
        case 0x384:  value = 0u; break;
        case 0x018:  value = identity.chip_identity; break;
        case 0x01c:  value = identity.features; break;
        case 0x020:  value = identity.model; break;
        case 0x024:  value = identity.revision; break;
        case 0x028:  value = identity.date; break;
        case 0x02c:  value = identity.time; break;
        case 0x030:  value = 0u; break;
        case 0x034:  value = identity.minor[0]; break;
        case 0x038:
        case 0x03c:  value = st_.regs_[off >> 2]; break;
        case 0x040:
        case 0x044:  value = 0u; break;
        case 0x048:  value = identity.specs[0]; break;
        case 0x074:  value = identity.minor[1]; break;
        case 0x080:  value = identity.specs[1]; break;
        case 0x084:  value = identity.minor[2]; break;
        case 0x088:  value = identity.minor[3]; break;
        case 0x08c:  value = identity.specs[2]; break;
        case 0x094:  value = identity.minor[4]; break;
        case 0x09c:  value = identity.specs[3]; break;
        case 0x0a0:  value = identity.minor[5]; break;
        case 0x0a8:  value = 0u; break;
        case 0x0dc:
            /* hmi_ktp400_mobile_v17 GALCORE probes HI +0xDC on the i.MX6 Vivante cores;
               etnaviv state_hi.xml HI omits this offset, so only this read32 probe is stubbed. */
            value = 0u;
            break;
        case 0x0e8:  value = 0u; break;
        case 0x100:
            /* etnaviv state_hi.xml PM.POWER_CONTROLS bit 0 enables module clock gating;
               etnaviv_gpu.c resets it to 0, then enable_mlcg() sets bit 0. */
            value = st_.regs_[off >> 2];
            break;
        case 0x104:
        case 0x10c:  value = st_.regs_[off >> 2]; break;
        case 0x400:
        case 0x404:
        case 0x408:
        case 0x40c:
        case 0x410:
        case 0x414:
        case 0x418:
        case 0x41c:
        case 0x420:
        case 0x424:
        case 0x428:
        case 0x42c:
        case 0x444:
        case 0x470:
        case 0x474:
        case 0x478:
        case 0x47c:
        case 0x480:  value = st_.regs_[off >> 2]; break;
        case 0x430:  value = 0u; break;
        case 0x438:
        case 0x43c:
        case 0x440:
        case 0x448:
        case 0x44c:
        case 0x450:
        case 0x454:
        case 0x458:
        case 0x45c:
        case 0x460:
        case 0x464:
        case 0x468:
        case 0x46c:  value = 0u; break;
        case 0x654:
        case 0x658:
        case 0x664:
        case 0x668:
        case 0x66c:  value = st_.regs_[off >> 2]; break;
        default:
            if (mem_->IsGpuStateOffset(off)) {
                value = mem_->StateReg(off);
                break;
            }
            HaltUnsupportedAccess("imx6-vivante read32 unmodelled register", addr, 0);
        }
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if (((addr - MmioBase()) & ~3u) == 0x100u)
            HaltUnsupportedAccess("write8 Vivante PM.POWER_CONTROLS", addr, value);
        Imx6ForEachMmioLane(addr, value, 1u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (((addr - MmioBase()) & ~3u) == 0x100u)
            HaltUnsupportedAccess("write16 Vivante PM.POWER_CONTROLS", addr, value);
        Imx6ForEachMmioLane(addr, value, 2u,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x000:
            st_.regs_[off >> 2] = value & ~0x00001000u;
            break;
        case 0x008:
            /* etnaviv state_hi.xml HI.AXI_CONFIG defines AWID/ARID/AWCACHE/ARCACHE in bits 0..15. */
            if ((value & 0xFFFF0000u) != 0u)
                HaltUnsupportedAccess("Vivante HI.AXI_CONFIG reserved bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x010:
            st_.intr_status_ &= ~value;
            mem_->UpdateIrq();
            break;
        case 0x014:
            st_.intr_enable_ = value;
            st_.regs_[off >> 2] = value;
            mem_->UpdateIrq();
            break;
        case 0x038:
        case 0x03c:  st_.regs_[off >> 2] = value; break;
        case 0x100:
            if ((value & ~1u) != 0u)
                HaltUnsupportedAccess("Vivante PM.POWER_CONTROLS", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x104:
        case 0x10c:
        case 0x400:
        case 0x404:
        case 0x408:
        case 0x40c:
        case 0x410:
        case 0x414:
        case 0x418:
        case 0x41c:
        case 0x420:
        case 0x424:
        case 0x428:
        case 0x42c:
        case 0x444:
        case 0x470:
        case 0x474:
        case 0x478:
        case 0x47c:
        case 0x480:
            st_.regs_[off >> 2] = value;
            break;
        case 0x430:
            mem_->FlushEngineCaches();
            st_.regs_[off >> 2] = 0u;
            break;
        case 0x654:  st_.regs_[off >> 2] = value; break;
        case 0x658:
            st_.regs_[off >> 2] = value & ~0x00010000u;
            if (value & kFeCommandEnable) {
                fe_->RunFrontend(value);
            }
            break;
        case 0x384:  st_.regs_[off >> 2] = 0u; break;
        default:
            if (mem_->IsGpuStateOffset(off)) {
                mem_->EnsureStateSize();
                mem_->StoreStateReg(off, value);
                if (off == 0x01294u && (value & (1u << 3)) == 0u) blit_->ExecuteVideoRasterizer(value & 3u);
                if (off == 0x01600u && value != 0u) blit_->ExecuteRs();
                if (off == 0x016B0u && value != 0u) blit_->ExecuteRsInPlace(value);
                break;
            }
            HaltUnsupportedAccess("imx6-vivante write32 unmodelled register", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        w.WriteBytes("regs", st_.regs_, sizeof(st_.regs_));
        w.Write("intr_status", st_.intr_status_);
        w.Write("intr_enable", st_.intr_enable_);
        uint32_t b = st_.irq_asserted_ ? 1u : 0u;
        w.Write("irq_asserted", b);
        b = st_.fe_live_ ? 1u : 0u;
        w.Write("fe_live", b);
        b = st_.fe_idle_ring_ ? 1u : 0u;
        w.Write("fe_idle_ring", b);
        w.Write("fe_ring_pc", st_.fe_ring_pc_);
        w.Write("fe_ring_prefetch", st_.fe_ring_prefetch_);
        w.Write("fe_window_words", st_.fe_window_words_);
        w.Write("fe_resume_idle_target", st_.fe_resume_idle_target_);
        uint32_t address_space = static_cast<uint32_t>(st_.fe_address_space_);
        w.Write("fe_address_space", address_space);
        address_space = static_cast<uint32_t>(st_.fe_resume_address_space_);
        w.Write("fe_resume_address_space", address_space);
        w.Write("fe_call_depth", st_.fe_call_depth_);
        for (const FeCallFrame& frame : st_.fe_call_stack_) {
            w.Write("frame_return_address", frame.return_address);
            w.Write("frame_return_window_words", frame.return_window_words);
            w.Write("frame_return_address_space", static_cast<uint32_t>(frame.return_address_space));
        }
        w.WriteBytes("semaphore_tokens", st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        w.WriteBytes("de_pattern_latch", st_.de_pattern_latch_, sizeof(st_.de_pattern_latch_));
        w.Write("de_pattern_latch_config", st_.de_pattern_latch_config_);
        w.Write("de_pattern_latch_address", st_.de_pattern_latch_address_);
        w.Write("de_pattern_latch_bpp", st_.de_pattern_latch_bpp_);
        b = st_.de_pattern_latch_valid_ ? 1u : 0u;
        w.Write("de_pattern_latch_valid", b);
        const uint32_t n = static_cast<uint32_t>(st_.state_.size());
        w.Write("state_count", n);
        if (n) w.WriteBytes("state", st_.state_.data(), n * sizeof(uint32_t));
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        r.ReadBytes("regs", st_.regs_, sizeof(st_.regs_));
        r.Read("intr_status", st_.intr_status_);
        r.Read("intr_enable", st_.intr_enable_);
        uint32_t b = 0;
        r.Read("irq_asserted", b);
        st_.irq_asserted_ = b != 0u;
        r.Read("fe_live", b);
        st_.fe_live_ = b != 0u;
        r.Read("fe_idle_ring", b);
        st_.fe_idle_ring_ = b != 0u;
        r.Read("fe_ring_pc", st_.fe_ring_pc_);
        r.Read("fe_ring_prefetch", st_.fe_ring_prefetch_);
        r.Read("fe_window_words", st_.fe_window_words_);
        r.Read("fe_resume_idle_target", st_.fe_resume_idle_target_);
        uint32_t address_space = 0u;
        r.Read("fe_address_space", address_space);
        st_.fe_address_space_ = address_space <= static_cast<uint32_t>(FeCommandAddressSpace::Virtual)
                                    ? static_cast<FeCommandAddressSpace>(address_space)
                                    : FeCommandAddressSpace::Physical;
        r.Read("fe_resume_address_space", address_space);
        st_.fe_resume_address_space_ = address_space <= static_cast<uint32_t>(FeCommandAddressSpace::Virtual)
                                           ? static_cast<FeCommandAddressSpace>(address_space)
                                           : FeCommandAddressSpace::Physical;
        r.Read("fe_call_depth", st_.fe_call_depth_);
        if (st_.fe_call_depth_ > kFeCallStackDepth) st_.fe_call_depth_ = 0u;
        for (FeCallFrame& frame : st_.fe_call_stack_) {
            r.Read("frame_return_address", frame.return_address);
            r.Read("frame_return_window_words", frame.return_window_words);
            uint32_t frame_address_space = 0u;
            r.Read("frame_return_address_space", frame_address_space);
            frame.return_address_space =
                frame_address_space <= static_cast<uint32_t>(FeCommandAddressSpace::Virtual)
                    ? static_cast<FeCommandAddressSpace>(frame_address_space)
                    : FeCommandAddressSpace::Physical;
        }
        r.ReadBytes("semaphore_tokens", st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        r.ReadBytes("de_pattern_latch", st_.de_pattern_latch_, sizeof(st_.de_pattern_latch_));
        r.Read("de_pattern_latch_config", st_.de_pattern_latch_config_);
        r.Read("de_pattern_latch_address", st_.de_pattern_latch_address_);
        r.Read("de_pattern_latch_bpp", st_.de_pattern_latch_bpp_);
        r.Read("de_pattern_latch_valid", b);
        st_.de_pattern_latch_valid_ = b != 0u;
        uint32_t n = 0;
        r.Read("state_count", n);
        st_.state_.resize(n);
        if (n) r.ReadBytes("state", st_.state_.data(), n * sizeof(uint32_t));
    }

    void PostRestore() override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        st_.fe_in_advance_ = false;
        mem_->UpdateIrq();
        if (st_.fe_live_) {
            std::lock_guard<std::mutex> g(fe_cv_mtx_);
            fe_cv_.notify_all();
        }
    }

    void FeLoop() {
        auto& freeze = emu_.Get<EmulationFreeze>();
        constexpr auto kInterval = std::chrono::microseconds(250);
        while (!fe_stop_.load(std::memory_order_acquire)) {
            {
                auto frozen = freeze.WorkerSection();
                std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
                if (st_.fe_live_) fe_->AdvanceFrontendRing();
            }
            std::unique_lock<std::mutex> g(fe_cv_mtx_);
            fe_cv_.wait_for(g, kInterval, [this] { return fe_stop_.load(std::memory_order_acquire); });
        }
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == 0x010u) {
            std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
            st_.intr_status_ &= ~lane.value;
            mem_->UpdateIrq();
            return;
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

private:

    bool FrontendBusy() const { return st_.fe_live_ && !st_.fe_idle_ring_; }

    VivanteState st_;
    std::unique_ptr<VivanteMem> mem_;
    std::unique_ptr<VivanteBlit> blit_;
    std::unique_ptr<VivanteFe> fe_;
    std::recursive_mutex fe_mutex_;
    std::thread fe_worker_;
    std::atomic<bool> fe_stop_{false};
    std::mutex fe_cv_mtx_;
    std::condition_variable fe_cv_;
};

}
