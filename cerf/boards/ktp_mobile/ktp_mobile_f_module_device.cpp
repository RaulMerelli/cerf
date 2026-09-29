#include "ktp_mobile_f_module_device.h"

#include "../board_context.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/cerf_paths.h"
#include "../../core/fatal.h"
#include "../../core/log.h"
#include "../../core/virtual_clock.h"
#include "../../cpu/emulated_memory.h"
#include "../../state/state_stream.h"
#include "ktp_mobile_board_profile.h"
#include "ktp_mobile_fwf_fsf_container.h"
#include "ktp_mobile_f_module_state_io.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <type_traits>
#include <vector>
#include "../../core/byte_order.h"

bool KtpMobileFModuleDevice::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    if (!board) return false;
    const auto* profile = TryKtpMobileBoardProfileFor(board->GetBoardId());
    return profile && profile->has_f_module;
}

void KtpMobileFModuleDevice::OnReady() {
    const auto& config = emu_.Get<DeviceConfig>();
    const std::string fwf_path = ResolveDeviceFile(config.device_name, config.rom_primary);
    std::ifstream input(fwf_path, std::ios::binary | std::ios::ate);
    if (!input.good())
        emu_.Get<Fatal>().Die("KTP Mobile F-module: cannot open FWF '%s'", fwf_path.c_str());
    const std::streamoff size = input.tellg();
    if (size <= 0)
        emu_.Get<Fatal>().Die("KTP Mobile F-module: empty FWF '%s'", fwf_path.c_str());
    std::vector<uint8_t> fwf(static_cast<size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(fwf.data()), size);
    if (!input)
        emu_.Get<Fatal>().Die("KTP Mobile F-module: cannot read FWF '%s'", fwf_path.c_str());

    std::vector<uint8_t> firmware;
    unsigned firmware_matches = 0;
    for (const auto& entry : ktp_mobile_fwf::ParseFsfVolume(fwf)) {
        if (_stricmp(entry.dir.c_str(), "AddOn") == 0 &&
            _stricmp(entry.name.c_str(), "komp2.upd") == 0) {
            firmware = entry.data;
            ++firmware_matches;
        }
    }
    if (firmware_matches != 1u) {
        emu_.Get<Fatal>().Die(
            "KTP Mobile F-module: expected one \\Flash\\AddOn\\komp2.upd in '%s', found %u",
            fwf_path.c_str(), firmware_matches);
    }
    const auto provisioned = model_.ConfigureFirmwareContainer(
        firmware.data(), firmware.size(), true);
    if (provisioned != ktp_mobile::Status::Ok) {
        emu_.Get<Fatal>().Die(
            "KTP Mobile F-module: invalid komp2.upd in '%s' (%u)", fwf_path.c_str(),
            static_cast<unsigned>(provisioned));
    }
    auto provisioned_state = std::make_unique<ktp_mobile::State>();
    model_.CaptureState(*provisioned_state);
    if (provisioned_state->approved_container_valid == 0u) {
        emu_.Get<Fatal>().Die(
            "KTP Mobile F-module: failed to retain selected FWF authorization");
    }
    selected_container_valid_ = true;
    selected_container_sha256_ = provisioned_state->approved_container_sha256;
    const auto installed = model_.InstalledFirmware();
    LOG(Board, "KTP Mobile F-module: materialized %.20s from FWF\n",
        reinterpret_cast<const char*>(installed.version.data()));
    cyclic_ready_timer_ = emu_.Get<VirtualTimerList>().Add(
        [this] { OnCyclicReady(); });
}

void KtpMobileFModuleDevice::OnShutdown() {
    if (cyclic_ready_timer_)
        cyclic_ready_timer_->Arm(VirtualTimerList::kNoDeadline);
}

bool KtpMobileFModuleDevice::ReadyLevel() const {
    return !reset_held_ && !cyclic_ready_suppressed_ &&
           model_.ModuleGpio5DataReady();
}

void KtpMobileFModuleDevice::SetReadyChangedCallback(ReadyChangedFn fn,
                                                     void* context) {
    ready_changed_ = fn;
    ready_changed_context_ = context;
}

void KtpMobileFModuleDevice::NotifyIfReadyChanged(bool old_ready) {
    if (old_ready != ReadyLevel() && ready_changed_)
        ready_changed_(ready_changed_context_);
}

void KtpMobileFModuleDevice::StageDmaTransmit(uint32_t buffer_pa,
                                              uint32_t bytes) {
    const bool old_ready = ReadyLevel();
    dma_tx_ready_ = false;
    dma_tx_bytes_ = 0;
    adapter_error_ = bytes != kTransferBytes || (bytes & 3u) != 0u;
    if (adapter_error_) return;

    auto& memory = emu_.Get<EmulatedMemory>();
    for (uint32_t off = 0; off < bytes; off += 4u) {
        uint8_t* src = memory.TryTranslateRange(buffer_pa + off, sizeof(uint32_t));
        if (!src) {
            adapter_error_ = true;
            return;
        }
        uint32_t word = 0;
        std::memcpy(&word, src, sizeof(word));
        dma_tx_[off + 0u] = static_cast<uint8_t>(word >> 24u);
        dma_tx_[off + 1u] = static_cast<uint8_t>(word >> 16u);
        dma_tx_[off + 2u] = static_cast<uint8_t>(word >> 8u);
        dma_tx_[off + 3u] = static_cast<uint8_t>(word);
    }
    dma_tx_bytes_ = bytes;
    dma_tx_ready_ = true;
    NotifyIfReadyChanged(old_ready);
}
void KtpMobileFModuleDevice::StageDmaReceive(uint32_t buffer_pa,
                                             uint32_t bytes) {
    dma_rx_buffer_pa_ = buffer_pa;
    dma_rx_bytes_ = bytes;
    dma_rx_pending_ = true;
    if (bytes != kTransferBytes || (bytes & 3u) != 0u)
        adapter_error_ = true;
    FlushDmaReceive();
}

void KtpMobileFModuleDevice::FlushDmaReceive() {
    if (!dma_rx_pending_ || !dma_response_ready_ || adapter_error_) return;
    auto& memory = emu_.Get<EmulatedMemory>();
    for (uint32_t off = 0; off < dma_rx_bytes_; off += 4u) {
        uint8_t* dst = memory.TryTranslateRange(dma_rx_buffer_pa_ + off, sizeof(uint32_t), true);
        if (!dst) {
            adapter_error_ = true;
            return;
        }
        const uint32_t word = cerf::be::U32(dma_rx_.data(), off);
        std::memcpy(dst, &word, sizeof(word));
    }
    dma_rx_pending_ = false;
    dma_response_ready_ = false;
}

bool KtpMobileFModuleDevice::Exchange(uint32_t conreg, uint32_t configreg) {
    const bool old_ready = ReadyLevel();
    if (reset_held_ || adapter_error_ || !dma_tx_ready_ ||
        dma_tx_bytes_ != kTransferBytes || model_cs_active_)
        return false;

    const uint32_t channel = (conreg >> 18u) & 3u;
    const uint32_t channel_mask = 1u << channel;
    const bool master = (conreg & (channel_mask << 4u)) != 0u;
    const bool idle_high = (configreg & (channel_mask << 4u)) != 0u;
    const bool phase_one = (configreg & channel_mask) != 0u;
    const bool clock_stays_high = (configreg & (channel_mask << 20u)) != 0u;
    const bool chip_select_active_high =
        (configreg & (channel_mask << 12u)) != 0u;
    const uint32_t burst_bits = ((conreg >> 20u) & 0xFFFu) + 1u;
    if (!master || idle_high || phase_one || clock_stays_high ||
        chip_select_active_high || burst_bits != kTransferBytes * 8u)
        return false;

    if (model_.SetChipSelect(true) != ktp_mobile::Status::Ok) return false;
    model_cs_active_ = true;
    if (panel_ack_high_ &&
        model_.SetPanelGpio6(true) != ktp_mobile::Status::Ok) {
        model_.SetChipSelect(false);
        model_cs_active_ = false;
        return false;
    }

    ktp_mobile::SpiTransferFormat format{};
    format.clock_polarity = idle_high
                                ? ktp_mobile::SpiClockPolarity::IdleHigh
                                : ktp_mobile::SpiClockPolarity::IdleLow;
    format.clock_phase = phase_one
                             ? ktp_mobile::SpiClockPhase::CaptureSecondEdge
                             : ktp_mobile::SpiClockPhase::CaptureFirstEdge;
    const auto result = model_.TransferSpi(
        dma_tx_.data(), dma_rx_.data(), dma_tx_.size(), format);
    if (result.status != ktp_mobile::Status::Ok ||
        result.bytes_transferred != dma_tx_.size()) {
        model_.SetChipSelect(false);
        model_cs_active_ = false;
        return false;
    }

    dma_tx_ready_ = false;
    serial_complete_ = true;
    dma_response_ready_ = true;
    FlushDmaReceive();

    FinishTransactionIfAcknowledged();
    NotifyIfReadyChanged(old_ready);
    if (!first_exchange_logged_) {
        first_exchange_logged_ = true;
        LOG(Board, "KTP Mobile F-module: first 272-byte ECSPI3 exchange completed\n");
    }
    return !adapter_error_;
}

void KtpMobileFModuleDevice::FinishTransactionIfAcknowledged() {
    if (!model_cs_active_ || !serial_complete_ || !panel_ack_high_) return;
    const auto status = model_.SetChipSelect(false);
    if (status != ktp_mobile::Status::Ok) {
        adapter_error_ = true;
        LOG(Board, "KTP Mobile F-module rejected the completed exchange (status=%u)\n",
            static_cast<unsigned>(status));
    }
    model_cs_active_ = false;
    serial_complete_ = false;
}

void KtpMobileFModuleDevice::ObservePanelAcknowledge(bool high) {
    const bool old_ready = ReadyLevel();
    panel_ack_high_ = high;
    if (high && !model_cs_active_) {
        NotifyIfReadyChanged(old_ready);
        return;
    }
    const auto status = model_.SetPanelGpio6(high);
    if (!high && status == ktp_mobile::Status::Ok)
        ArmCyclicReady();
    if (high && status == ktp_mobile::Status::Ok) {
        FinishTransactionIfAcknowledged();
    }
    NotifyIfReadyChanged(old_ready);
}

/* komp2 firmware 0x0803423A..0x080342A0 programs TIM3 with PSC=0, ARR=0xEA60 and
   CR1=CEN|ARPE; RCC_PLLCFGR at 0x08034A12 is 0x04413C18, so the timer clock is
   60 MHz and the update period is 1 ms. */
void KtpMobileFModuleDevice::ArmCyclicReady() {
    constexpr int64_t kCyclicPeriodNs = 1'000'000;
    cyclic_ready_suppressed_ = true;
    cyclic_ready_deadline_ns_ =
        emu_.Get<VirtualClock>().NowNs() + kCyclicPeriodNs;
    cyclic_ready_timer_->Arm(cyclic_ready_deadline_ns_);
}

void KtpMobileFModuleDevice::OnCyclicReady() {
    const bool old_ready = ReadyLevel();
    cyclic_ready_suppressed_ = false;
    cyclic_ready_deadline_ns_ = VirtualTimerList::kNoDeadline;
    NotifyIfReadyChanged(old_ready);
}

void KtpMobileFModuleDevice::ClearWireTransaction() {
    dma_tx_bytes_ = 0;
    dma_rx_buffer_pa_ = 0;
    dma_rx_bytes_ = 0;
    dma_tx_ready_ = false;
    dma_rx_pending_ = false;
    dma_response_ready_ = false;
    serial_complete_ = false;
    model_cs_active_ = false;
    adapter_error_ = false;
    cyclic_ready_suppressed_ = false;
    cyclic_ready_deadline_ns_ = VirtualTimerList::kNoDeadline;
    if (cyclic_ready_timer_)
        cyclic_ready_timer_->Arm(VirtualTimerList::kNoDeadline);
    dma_tx_.fill(0u);
    dma_rx_.fill(0u);
}

void KtpMobileFModuleDevice::ObserveModuleReset(bool high) {
    const bool old_ready = ReadyLevel();
    if (!high) {
        reset_held_ = true;
        reset_low_seen_ = true;
    } else if (reset_held_) {
        reset_held_ = false;
        if (reset_low_seen_) {
            model_.WarmModuleReset();
            ClearWireTransaction();
        }
        reset_low_seen_ = false;
    }
    NotifyIfReadyChanged(old_ready);
}

void KtpMobileFModuleDevice::SaveState(StateWriter& w) {
    auto state = std::make_unique<ktp_mobile::State>();
    model_.CaptureState(*state);
    ktp_mobile_f_module_state_io::Write(w, *state);
    w.WriteBytes("dma_tx", dma_tx_.data(), dma_tx_.size());
    w.WriteBytes("dma_rx", dma_rx_.data(), dma_rx_.size());
    w.Write("dma_tx_bytes", dma_tx_bytes_);
    w.Write("dma_rx_buffer_pa", dma_rx_buffer_pa_);
    w.Write("dma_rx_bytes", dma_rx_bytes_);
    const uint8_t flags[] = {
        static_cast<uint8_t>(dma_tx_ready_),
        static_cast<uint8_t>(dma_rx_pending_),
        static_cast<uint8_t>(dma_response_ready_),
        static_cast<uint8_t>(serial_complete_),
        static_cast<uint8_t>(model_cs_active_),
        static_cast<uint8_t>(panel_ack_high_),
        static_cast<uint8_t>(reset_held_),
        static_cast<uint8_t>(reset_low_seen_),
        static_cast<uint8_t>(adapter_error_),
        static_cast<uint8_t>(cyclic_ready_suppressed_),
    };
    w.WriteBytes("flags", flags, sizeof(flags));
    const int64_t now = emu_.Get<VirtualClock>().NowNs();
    w.Write("cyclic_ready_remaining_ns", cyclic_ready_timer_->RemainingNs(now));
}

void KtpMobileFModuleDevice::RestoreState(StateReader& r) {
    auto state = std::make_unique<ktp_mobile::State>();
    ktp_mobile_f_module_state_io::Read(r, *state);
    r.ReadBytes("dma_tx", dma_tx_.data(), dma_tx_.size());
    r.ReadBytes("dma_rx", dma_rx_.data(), dma_rx_.size());
    uint32_t dma_tx_bytes = 0;
    uint32_t dma_rx_buffer_pa = 0;
    uint32_t dma_rx_bytes = 0;
    r.Read("dma_tx_bytes", dma_tx_bytes);
    r.Read("dma_rx_buffer_pa", dma_rx_buffer_pa);
    r.Read("dma_rx_bytes", dma_rx_bytes);
    uint8_t flags[10]{};
    r.ReadBytes("flags", flags, sizeof(flags));
    int64_t cyclic_ready_remaining_ns = VirtualTimerList::kNoDeadline;
    r.Read("cyclic_ready_remaining_ns", cyclic_ready_remaining_ns);

    if (!r.Ok())
        r.Reject("F-module: truncated saved state");
    for (uint8_t flag : flags) {
        if (flag > 1u)
            r.Reject("F-module: adapter flag %u is not a boolean", flag);
    }

    const bool dma_tx_ready = flags[0] != 0u;
    const bool dma_rx_pending = flags[1] != 0u;
    const bool dma_response_ready = flags[2] != 0u;
    const bool serial_complete = flags[3] != 0u;
    const bool model_cs_active = flags[4] != 0u;
    const bool reset_held = flags[6] != 0u;
    const bool reset_low_seen = flags[7] != 0u;
    const bool cyclic_ready_suppressed = flags[9] != 0u;
    const bool lengths_valid =
        dma_tx_bytes <= kTransferBytes && (dma_tx_bytes & 3u) == 0u &&
        dma_rx_bytes <= kTransferBytes && (dma_rx_bytes & 3u) == 0u;
    const bool receive_address_valid =
        dma_rx_bytes == 0u ||
        dma_rx_buffer_pa <= UINT32_MAX - (dma_rx_bytes - 1u);
    const bool transaction_valid =
        (!dma_tx_ready || dma_tx_bytes == kTransferBytes) &&
        (!dma_rx_pending || dma_rx_bytes == kTransferBytes) &&
        (!dma_response_ready || !dma_tx_ready) &&
        serial_complete == model_cs_active &&
        model_cs_active == (state->chip_select_asserted != 0u) &&
        (!reset_low_seen || reset_held) && receive_address_valid;
    const bool timer_valid =
        cyclic_ready_suppressed
            ? cyclic_ready_remaining_ns != VirtualTimerList::kNoDeadline
            : cyclic_ready_remaining_ns == VirtualTimerList::kNoDeadline;
    if (!lengths_valid || !transaction_valid || !timer_valid) {
        r.Reject("F-module: invalid saved adapter state");
    }
    if (!selected_container_valid_ || state->approved_container_valid == 0u ||
        state->approved_container_sha256 != selected_container_sha256_) {
        r.Reject("F-module: saved state does not match the selected FWF firmware");
    }
    const auto status = model_.RestoreState(*state);
    if (status != ktp_mobile::Status::Ok)
        r.Reject("F-module: invalid saved model state (%u)", static_cast<unsigned>(status));

    dma_tx_bytes_ = dma_tx_bytes;
    dma_rx_buffer_pa_ = dma_rx_buffer_pa;
    dma_rx_bytes_ = dma_rx_bytes;
    dma_tx_ready_ = dma_tx_ready;
    dma_rx_pending_ = dma_rx_pending;
    dma_response_ready_ = dma_response_ready;
    serial_complete_ = serial_complete;
    model_cs_active_ = model_cs_active;
    panel_ack_high_ = flags[5] != 0u;
    reset_held_ = reset_held;
    reset_low_seen_ = reset_low_seen;
    adapter_error_ = flags[8] != 0u;
    cyclic_ready_suppressed_ = cyclic_ready_suppressed;
    restored_cyclic_ready_remaining_ns_ = cyclic_ready_remaining_ns;
    cyclic_ready_deadline_ns_ = VirtualTimerList::kNoDeadline;
}

void KtpMobileFModuleDevice::PostRestore() {
    FlushDmaReceive();
    if (cyclic_ready_suppressed_) {
        const int64_t now = emu_.Get<VirtualClock>().NowNs();
        cyclic_ready_deadline_ns_ = VirtualTimerList::DeadlineFromRemainingNs(
            now, restored_cyclic_ready_remaining_ns_);
    } else {
        cyclic_ready_deadline_ns_ = VirtualTimerList::kNoDeadline;
    }
    cyclic_ready_timer_->Arm(cyclic_ready_deadline_ns_);
    restored_cyclic_ready_remaining_ns_ = VirtualTimerList::kNoDeadline;
    if (ready_changed_) ready_changed_(ready_changed_context_);
}

REGISTER_SERVICE_AS(KtpMobileFModuleDevice, Imx6EcspiEndpoint);
