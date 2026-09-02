#pragma once

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../boards/board_context.h"
#include "../../peripherals/peripheral_base.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../../state/emulation_freeze.h"
#include "imx6_vivante_state.h"
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

namespace {

using namespace imx6_vivante;

/* Vivante GC front-end model for the i.MX6 GPU blocks.
   Register bases / IRQs (NXP i.MX6 Solo/DL memory map, Linux imx6qdl.dtsi):
   GPU3D 0x00130000/SPI9, GPU2D 0x00134000/SPI10, OpenVG 0x02204000/SPI11.
   This Peripheral owns the register/FE state and the recursive_mutex; the
   VivanteMem / VivanteBlit / VivanteFe helpers run the engine under that lock. */
template <uint32_t kBase, VivanteCore kCore, int kIrqSpi> class Imx6Gpu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSoc() == SocFamily::iMX6;
    }
    void OnReady() override {
        mem_ = std::make_unique<VivanteMem>(st_, emu_, *this, Core(), IrqSpi());
        blit_ = std::make_unique<VivanteBlit>(st_, *mem_, emu_);
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
        return static_cast<uint8_t>(ReadWord(addr & ~3u) >> ((addr & 3u) * 8u));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return static_cast<uint16_t>(ReadWord(addr & ~3u) >> ((addr & 2u) * 8u));
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
        case 0x000: { /* VIVS_HI_CLOCK_CONTROL */
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
        case 0x004: { /* VIVS_HI_IDLE_STATE */
            value = 0x8007FFFFu;
            if (FrontendBusy()) {
                if (Core() == VivanteCore::Gc3202d) {
                    /* FE, DE and PE are active while a 2D packet is pending.
                       The WinCE queue manager requires DE..TX all idle before
                       recycling a command buffer. */
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
        case 0x008: /* VIVS_HI_AXI_CONFIG */ value = st_.regs_[off >> 2]; break;
        case 0x00c: /* VIVS_HI_AXI_STATUS */ value = 0u; break;
        case 0x010: /* VIVS_HI_INTR_ACKNOWLEDGE */
            value = st_.intr_status_;
            if (st_.intr_status_ != 0u) {
                st_.intr_status_ = 0u;
                mem_->UpdateIrq();
            }
            break;
        case 0x014: /* VIVS_HI_INTR_ENBL */ value = st_.intr_enable_; break;
        case 0x108: /* VIVS_PM_MODULE_STATUS */ value = 0u; break;
        case 0x188: /* VIVS_MMUv2_STATUS */ value = st_.regs_[off >> 2]; break;
        case 0x190: /* VIVS_MMUv2_EXCEPTION_ADDR[0] */
        case 0x194: /* VIVS_MMUv2_EXCEPTION_ADDR[1] */
        case 0x198: /* VIVS_MMUv2_EXCEPTION_ADDR[2] */
        case 0x19c: /* VIVS_MMUv2_EXCEPTION_ADDR[3] */ value = st_.regs_[off >> 2]; break;
        case 0x384: /* VIVS_MMUv2_SEC_STATUS */ value = 0u; break;
        case 0x180: /* VIVS_MMUv2_SAFE_ADDRESS */ value = st_.regs_[off >> 2]; break;
        case 0x18c: /* VIVS_MMUv2_CONTROL */ value = st_.regs_[off >> 2] & 1u; break;
        case 0x018: /* VIVS_HI_CHIP_IDENTITY (obsolete on these cores) */ value = identity.chip_identity; break;
        case 0x01c: /* VIVS_HI_CHIP_FEATURE */ value = identity.features; break;
        case 0x020: /* VIVS_HI_CHIP_MODEL */ value = identity.model; break;
        case 0x024: /* VIVS_HI_CHIP_REV */ value = identity.revision; break;
        case 0x028: /* VIVS_HI_CHIP_DATE */ value = identity.date; break;
        case 0x02c: /* VIVS_HI_CHIP_TIME */ value = identity.time; break;
        case 0x030: /* VIVS_HI_CHIP_CUSTOMER_ID */ value = 0u; break;
        case 0x034: /* VIVS_HI_CHIP_MINOR_FEATURE_0 */ value = identity.minor[0]; break;
        case 0x038: /* VIVS_HI_CACHE_CONTROL */
        case 0x03c: /* VIVS_HI_MEMORY_COUNTER_RESET */ value = st_.regs_[off >> 2]; break;
        case 0x040: /* VIVS_HI_PROFILE_READ_BYTES8 */
        case 0x044: /* VIVS_HI_PROFILE_WRITE_BYTES8 */ value = 0u; break;
        case 0x048: /* VIVS_HI_CHIP_SPECS */ value = identity.specs[0]; break;
        case 0x074: /* VIVS_HI_CHIP_MINOR_FEATURE_1 */ value = identity.minor[1]; break;
        case 0x080: /* VIVS_HI_CHIP_SPECS_2 */ value = identity.specs[1]; break;
        case 0x084: /* VIVS_HI_CHIP_MINOR_FEATURE_2 */ value = identity.minor[2]; break;
        case 0x088: /* VIVS_HI_CHIP_MINOR_FEATURE_3 */ value = identity.minor[3]; break;
        case 0x08c: /* VIVS_HI_CHIP_SPECS_3 */ value = identity.specs[2]; break;
        case 0x094: /* VIVS_HI_CHIP_MINOR_FEATURE_4 */ value = identity.minor[4]; break;
        case 0x09c: /* VIVS_HI_CHIP_SPECS_4 */ value = identity.specs[3]; break;
        case 0x0a0: /* VIVS_HI_CHIP_MINOR_FEATURE_5 */ value = identity.minor[5]; break;
        case 0x0a8: /* VIVS_HI_CHIP_PRODUCT_ID */
        case 0x0e8: /* VIVS_HI_CHIP_ECO_ID */ value = 0u; break;
        case 0x104: /* VIVS_PM_MODULE_CONTROLS */ value = st_.regs_[off >> 2] | 0x004304FFu; break;
        case 0x10c: /* VIVS_PM_PULSE_EATER */ value = st_.regs_[off >> 2] ? st_.regs_[off >> 2] : 0x01590880u; break;
        case 0x400: /* VIVS_HI_MMU_FE_PAGE_TABLE */
        case 0x404: /* VIVS_HI_MMU_TX_PAGE_TABLE */
        case 0x408: /* VIVS_HI_MMU_PE_PAGE_TABLE */
        case 0x40c: /* VIVS_HI_MMU_PEZ_PAGE_TABLE */
        case 0x410: /* VIVS_HI_MMU_RA_PAGE_TABLE */
        case 0x414: /* VIVS_HI_DEBUG_MEMORY */
        case 0x418: /* VIVS_HI_MEMORY_BASE_ADDR_RA */
        case 0x41c: /* VIVS_HI_MEMORY_BASE_ADDR_FE */
        case 0x420: /* VIVS_HI_MEMORY_BASE_ADDR_TX */
        case 0x424: /* VIVS_HI_MEMORY_BASE_ADDR_PEZ */
        case 0x428: /* VIVS_HI_MEMORY_BASE_ADDR_PE */
        case 0x42c: /* VIVS_HI_MEMORY_TIMING_CONTROL */
        case 0x444: /* VIVS_HI_DEBUG_WRITE */
        case 0x470: /* VIVS_HI_PROFILE_CONFIG0 */
        case 0x474: /* VIVS_HI_PROFILE_CONFIG1 */
        case 0x478: /* VIVS_HI_PROFILE_CONFIG2 */
        case 0x47c: /* VIVS_HI_PROFILE_CONFIG3 */
        case 0x480: /* VIVS_HI_BUS_CONFIG */ value = st_.regs_[off >> 2]; break;
        case 0x430: /* VIVS_HI_MEMORY_FLUSH: self-clearing pulse */ value = 0u; break;
        case 0x438: /* VIVS_HI_PROFILE_CYCLE_COUNTER */
        case 0x43c: /* VIVS_HI_DEBUG_READ0 */
        case 0x440: /* VIVS_HI_DEBUG_READ1 */
        case 0x448: /* VIVS_HI_PROFILE_RA_READ */
        case 0x44c: /* VIVS_HI_PROFILE_TX_READ */
        case 0x450: /* VIVS_HI_PROFILE_FE_READ */
        case 0x454: /* VIVS_HI_PROFILE_PE_READ */
        case 0x458: /* VIVS_HI_PROFILE_DE_READ */
        case 0x45c: /* VIVS_HI_PROFILE_SH_READ */
        case 0x460: /* VIVS_HI_PROFILE_PA_READ */
        case 0x464: /* VIVS_HI_PROFILE_SE_READ */
        case 0x468: /* VIVS_HI_PROFILE_MC_READ */
        case 0x46c: /* VIVS_HI_PROFILE_HI_READ */ value = 0u; break;
        case 0x654: /* VIVS_FE_COMMAND_ADDRESS */
        case 0x658: /* VIVS_FE_COMMAND_CONTROL */
        case 0x664: /* VIVS_FE_DMA_ADDRESS */
        case 0x668: /* VIVS_FE_DMA_LOW */
        case 0x66c: /* VIVS_FE_DMA_HIGH */ value = st_.regs_[off >> 2]; break;
        default:
            if (mem_->IsGpuStateOffset(off)) {
                value = mem_->StateReg(off);
                break;
            }
            HaltUnsupportedAccess("imx6-vivante read32 unmodelled register", addr, 0);
        }
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override { MergeWrite(addr, value, 1); }
    void WriteHalf(uint32_t addr, uint16_t value) override { MergeWrite(addr, value, 2); }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x000: /* soft-reset bit self-clears; idle bits remain read-only high */
            if ((value & 0x00001000u) != 0u) mem_->ResetMmuv2State();
            st_.regs_[off >> 2] = value & ~0x00001000u;
            break;
        case 0x010: /* interrupt acknowledge is W1C. */
            st_.intr_status_ &= ~value;
            mem_->UpdateIrq();
            break;
        case 0x014: /* VIVS_HI_INTR_ENBL */
            st_.intr_enable_ = value;
            st_.regs_[off >> 2] = value;
            mem_->UpdateIrq();
            break;
        case 0x038: /* VIVS_HI_CACHE_CONTROL */
        case 0x03c: /* VIVS_HI_MEMORY_COUNTER_RESET */ st_.regs_[off >> 2] = value; break;
        case 0x184: /* VIVS_MMUv2_CONFIGURATION, rnndb masked register */ mem_->WriteMmuv2Configuration(value); break;
        case 0x180: /* VIVS_MMUv2_SAFE_ADDRESS: write-once after reset */ mem_->WriteMmuv2SafeAddress(value); break;
        case 0x104: /* VIVS_PM_MODULE_CONTROLS */
        case 0x10c: /* VIVS_PM_PULSE_EATER */
        case 0x400: /* VIVS_HI_MMU_FE_PAGE_TABLE */
        case 0x404: /* VIVS_HI_MMU_TX_PAGE_TABLE */
        case 0x408: /* VIVS_HI_MMU_PE_PAGE_TABLE */
        case 0x40c: /* VIVS_HI_MMU_PEZ_PAGE_TABLE */
        case 0x410: /* VIVS_HI_MMU_RA_PAGE_TABLE */
        case 0x414: /* VIVS_HI_DEBUG_MEMORY */
        case 0x418: /* VIVS_HI_MEMORY_BASE_ADDR_RA */
        case 0x41c: /* VIVS_HI_MEMORY_BASE_ADDR_FE */
        case 0x420: /* VIVS_HI_MEMORY_BASE_ADDR_TX */
        case 0x424: /* VIVS_HI_MEMORY_BASE_ADDR_PEZ */
        case 0x428: /* VIVS_HI_MEMORY_BASE_ADDR_PE */
        case 0x42c: /* VIVS_HI_MEMORY_TIMING_CONTROL */
        case 0x444: /* VIVS_HI_DEBUG_WRITE */
        case 0x470: /* VIVS_HI_PROFILE_CONFIG0 */
        case 0x474: /* VIVS_HI_PROFILE_CONFIG1 */
        case 0x478: /* VIVS_HI_PROFILE_CONFIG2 */
        case 0x47c: /* VIVS_HI_PROFILE_CONFIG3 */
        case 0x480: /* VIVS_HI_BUS_CONFIG */
            st_.regs_[off >> 2] = value;
            if (off >= 0x400u && off <= 0x410u) mem_->InvalidateTranslationCache();
            break;
        case 0x430: /* VIVS_HI_MEMORY_FLUSH */
            mem_->FlushEngineCaches(value);
            st_.regs_[off >> 2] = 0u;
            break;
        case 0x18c: { /* VIVS_MMUv2_CONTROL: once enabled, real hardware keeps it enabled */
            const uint32_t old = st_.regs_[off >> 2];
            st_.regs_[off >> 2] |= value & 1u;
            if (st_.regs_[off >> 2] != old) mem_->InvalidateTranslationCache();
            break;
        }
        case 0x654: /* VIVS_FE_COMMAND_ADDRESS */ st_.regs_[off >> 2] = value; break;
        case 0x658:                                     /* VIVS_FE_COMMAND_CONTROL */
            st_.regs_[off >> 2] = value & ~0x00010000u; /* command doorbell self-clears */
            if (value & kFeCommandEnable) {
                fe_->RunFrontend(value);
            }
            break;
        case 0x188: /* VIVS_MMUv2_STATUS: acknowledge all fault slots */
            st_.regs_[off >> 2] = 0u;
            st_.intr_status_ &= ~kMmuv2Interrupt;
            mem_->UpdateIrq();
            break;
        case 0x384: /* secure MMU is not exposed on the i.MX6 profiles */ st_.regs_[off >> 2] = 0u; break;
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
        w.WriteBytes(st_.regs_, sizeof(st_.regs_));
        w.Write(st_.intr_status_);
        w.Write(st_.intr_enable_);
        uint32_t b = st_.irq_asserted_ ? 1u : 0u;
        w.Write(b);
        b = st_.mmu_safe_address_written_ ? 1u : 0u;
        w.Write(b);
        b = st_.fe_live_ ? 1u : 0u;
        w.Write(b);
        b = st_.fe_idle_ring_ ? 1u : 0u;
        w.Write(b);
        w.Write(st_.fe_ring_pc_);
        w.Write(st_.fe_ring_prefetch_);
        w.Write(st_.fe_window_words_);
        w.Write(st_.fe_resume_idle_target_);
        uint32_t address_space = static_cast<uint32_t>(st_.fe_address_space_);
        w.Write(address_space);
        address_space = static_cast<uint32_t>(st_.fe_resume_address_space_);
        w.Write(address_space);
        w.Write(st_.fe_call_depth_);
        w.WriteBytes(st_.fe_call_stack_, sizeof(st_.fe_call_stack_));
        w.WriteBytes(st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        w.Write(st_.chip_select_mask_);
        w.WriteBytes(st_.de_pattern_latch_, sizeof(st_.de_pattern_latch_));
        w.Write(st_.de_pattern_latch_config_);
        w.Write(st_.de_pattern_latch_address_);
        w.Write(st_.de_pattern_latch_bpp_);
        b = st_.de_pattern_latch_valid_ ? 1u : 0u;
        w.Write(b);
        const uint32_t n = static_cast<uint32_t>(st_.state_.size());
        w.Write(n);
        if (n) w.WriteBytes(st_.state_.data(), n * sizeof(uint32_t));
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        r.ReadBytes(st_.regs_, sizeof(st_.regs_));
        r.Read(st_.intr_status_);
        r.Read(st_.intr_enable_);
        uint32_t b = 0;
        r.Read(b);
        st_.irq_asserted_ = b != 0u;
        r.Read(b);
        st_.mmu_safe_address_written_ = b != 0u;
        r.Read(b);
        st_.fe_live_ = b != 0u;
        r.Read(b);
        st_.fe_idle_ring_ = b != 0u;
        r.Read(st_.fe_ring_pc_);
        r.Read(st_.fe_ring_prefetch_);
        r.Read(st_.fe_window_words_);
        r.Read(st_.fe_resume_idle_target_);
        uint32_t address_space = 0u;
        r.Read(address_space);
        st_.fe_address_space_ = address_space <= static_cast<uint32_t>(FeCommandAddressSpace::Virtual)
                                    ? static_cast<FeCommandAddressSpace>(address_space)
                                    : FeCommandAddressSpace::Physical;
        r.Read(address_space);
        st_.fe_resume_address_space_ = address_space <= static_cast<uint32_t>(FeCommandAddressSpace::Virtual)
                                           ? static_cast<FeCommandAddressSpace>(address_space)
                                           : FeCommandAddressSpace::Physical;
        r.Read(st_.fe_call_depth_);
        if (st_.fe_call_depth_ > kFeCallStackDepth) st_.fe_call_depth_ = 0u;
        r.ReadBytes(st_.fe_call_stack_, sizeof(st_.fe_call_stack_));
        r.ReadBytes(st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        r.Read(st_.chip_select_mask_);
        r.ReadBytes(st_.de_pattern_latch_, sizeof(st_.de_pattern_latch_));
        r.Read(st_.de_pattern_latch_config_);
        r.Read(st_.de_pattern_latch_address_);
        r.Read(st_.de_pattern_latch_bpp_);
        r.Read(b);
        st_.de_pattern_latch_valid_ = b != 0u;
        uint32_t n = 0;
        r.Read(n);
        st_.state_.resize(n);
        if (n) r.ReadBytes(st_.state_.data(), n * sizeof(uint32_t));
    }

    void PostRestore() override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        st_.fe_in_advance_ = false;
        mem_->InvalidateTranslationCache();
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

    void MergeWrite(uint32_t addr, uint32_t value, uint32_t width) {
        const uint32_t aligned = addr & ~3u;
        const uint32_t shift = (addr & 3u) * 8u;
        const uint32_t mask = (width == 1 ? 0xFFu : 0xFFFFu) << shift;
        WriteWord(aligned, (ReadWord(aligned) & ~mask) | ((value << shift) & mask));
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

} /* namespace */
