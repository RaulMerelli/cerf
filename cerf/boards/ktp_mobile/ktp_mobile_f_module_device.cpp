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
#include "../../peripherals/sd_card/fwf_fsf_container.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

bool IsFModuleBoard(Board board) {
    switch (board) {
    case Board::HmiKtp400FMobile:
    case Board::HmiKtp700FMobile:
    case Board::HmiKtp900FMobile:
    case Board::HmiKtp700FHwMobile:
    case Board::HmiKtp700FArcticMobile:
    case Board::HmiTp1000fMobile: return true;
    default: return false;
    }
}

} // namespace

bool KtpMobileFModuleDevice::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && IsFModuleBoard(board->GetBoard());
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
    for (const auto& entry : fwf_fsf::ParseFsfVolume(fwf)) {
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
        uint8_t* src = memory.TryTranslate(buffer_pa + off);
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
        uint8_t* dst = memory.TryTranslateWrite(dma_rx_buffer_pa_ + off);
        if (!dst) {
            adapter_error_ = true;
            return;
        }
        const uint32_t word =
            (static_cast<uint32_t>(dma_rx_[off + 0u]) << 24u) |
            (static_cast<uint32_t>(dma_rx_[off + 1u]) << 16u) |
            (static_cast<uint32_t>(dma_rx_[off + 2u]) << 8u) |
            static_cast<uint32_t>(dma_rx_[off + 3u]);
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

    /* IMX6DQRM Rev.2, sections 21.7.3-21.7.4: CHANNEL_SELECT is
       CONREG[19:18], its master bit is CONREG[4+channel], BURST_LENGTH is
       CONREG[31:20], and CONFIGREG holds per-channel SCLK_CTL[23:20],
       SS_POL[15:12], SCLK_POL[7:4] and SCLK_PHA[3:0]. */
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

void KtpMobileFModuleDevice::ArmCyclicReady() {
    constexpr int64_t kCyclicDelayNs = 1'000'000;
    cyclic_ready_suppressed_ = true;
    cyclic_ready_deadline_ns_ =
        emu_.Get<VirtualClock>().NowNs() + kCyclicDelayNs;
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
