#include "ktp_mobile_f_module_model.h"
#include "ktp_mobile_f_module_state.hpp"

#include <memory>
#include <new>
#include <utility>

namespace ktp_mobile {
using namespace detail;

struct KtpMobileFModule::Impl {
    State state{};

    Impl() noexcept {
        ResetVolatile(state, ResetKind::Cold);
    }
};

KtpMobileFModule::KtpMobileFModule() : impl_(std::make_unique<Impl>()) {}
KtpMobileFModule::~KtpMobileFModule() = default;
KtpMobileFModule::KtpMobileFModule(KtpMobileFModule&&) noexcept = default;
KtpMobileFModule& KtpMobileFModule::operator=(KtpMobileFModule&&) noexcept = default;

void KtpMobileFModule::ColdReset() noexcept {
    if (impl_ != nullptr) {
        ResetVolatile(impl_->state, ResetKind::Cold);
    }
}

void KtpMobileFModule::WarmModuleReset() noexcept {
    if (impl_ != nullptr) {
        ResetVolatile(impl_->state, ResetKind::WarmModule);
    }
}

Status KtpMobileFModule::ConfigureFirmwareContainer(
    const std::uint8_t* container, std::size_t length, bool install) noexcept {
    if (impl_ == nullptr) {
        return Status::InvalidState;
    }
    State& state = impl_->state;
    if (container == nullptr || !ValidateContainerStructure(container, length)) {
        return Status::InvalidArgument;
    }
    if (state.chip_select_asserted != 0u || state.update_phase != UpdatePhase::Inactive) {
        return Status::InvalidState;
    }
    state.approved_container_valid = 1u;
    state.reserved_approved.fill(0u);
    state.approved_container_sha256 = Sha256(container, length);
    if (!install) return Status::Ok;

    state.application_flash.fill(0xFFu);
    std::copy_n(container + kUpdateContainerPrefixBytes, kKnownPayloadBytes,
                state.application_flash.data());
    state.firmware.valid = 1u;
    state.firmware.flash_materialized = 1u;
    state.firmware.reserved = 0u;
    state.firmware.container_size = static_cast<std::uint32_t>(length);
    state.firmware.payload_size = kKnownPayloadBytes;
    std::copy_n(container + kKnownVersionOffset, state.firmware.version.size(),
                state.firmware.version.begin());
    return Status::Ok;
}

Status KtpMobileFModule::SetChipSelect(bool asserted) noexcept {
    if (impl_ == nullptr) {
        return Status::InvalidState;
    }
    State& state = impl_->state;

    if (asserted) {
        if (state.chip_select_asserted != 0u || state.gpio5_ready == 0u ||
            state.gpio6_ack != 0u) {
            return Status::InvalidState;
        }
        state.spi_rx.fill(0u);
        BuildResponseFrame(state, state.spi_tx);
        state.spi_bytes_transferred = 0u;
        state.chip_select_asserted = 1u;
        state.gpio5_ready = 1u;
        return Status::Ok;
    }

    if (state.chip_select_asserted == 0u) {
        return Status::InvalidState;
    }

    if (state.spi_bytes_transferred < kWireTransactionBytes) {
        ClearPhysicalTransaction(state);
        if (state.gpio6_ack != 0u) {
            state.gpio5_ready = 0u;
        } else {
            RecomputeReady(state);
        }
        return Status::IncompleteTransaction;
    }

    if (state.gpio6_ack == 0u) {
        ClearPhysicalTransaction(state);
        RecomputeReady(state);
        return Status::InvalidState;
    }

    const auto request = state.spi_rx;
    const auto response = state.spi_tx;
    const Status status = CommitWireRequest(state, request, response);
    ClearPhysicalTransaction(state);
    state.gpio5_ready = 0u;
    return status;
}

SpiTransferResult KtpMobileFModule::TransferSpi(
    const std::uint8_t* panel_tx,
    std::uint8_t* panel_rx,
    std::size_t byte_count,
    SpiTransferFormat format) noexcept {
    SpiTransferResult result{};
    if (impl_ == nullptr) {
        result.status = Status::InvalidState;
        return result;
    }
    State& state = impl_->state;

    if (byte_count != 0u && (panel_tx == nullptr || panel_rx == nullptr)) {
        result.status = Status::InvalidArgument;
        return result;
    }
    if (state.chip_select_asserted == 0u) {
        result.status = Status::InvalidState;
        return result;
    }
    if (!IsSupportedSpiFormat(format)) {
        result.status = Status::UnsupportedSpiFormat;
        return result;
    }
    if (byte_count > kWireTransactionBytes - state.spi_bytes_transferred) {
        result.status = Status::TransferWouldOverflow;
        return result;
    }

    const std::size_t offset = state.spi_bytes_transferred;
    if (byte_count != 0u) {
        std::copy_n(panel_tx, byte_count, state.spi_rx.data() + offset);
        std::copy_n(state.spi_tx.data() + offset, byte_count, panel_rx);
    }
    state.spi_bytes_transferred = static_cast<std::uint16_t>(offset + byte_count);
    result.bytes_transferred = byte_count;
    return result;
}

Status KtpMobileFModule::SetPanelGpio6(bool high) noexcept {
    if (impl_ == nullptr) {
        return Status::InvalidState;
    }
    State& state = impl_->state;
    const std::uint8_t desired = high ? 1u : 0u;
    if (state.gpio6_ack == desired) {
        return Status::Ok;
    }

    if (high) {
        if (state.chip_select_asserted == 0u) {
            return Status::InvalidState;
        }
        state.gpio6_ack = 1u;
        return Status::Ok;
    }

    if (state.chip_select_asserted != 0u) {
        return Status::InvalidState;
    }
    state.gpio6_ack = 0u;
    RecomputeReady(state);
    return Status::Ok;
}

bool KtpMobileFModule::ModuleGpio5DataReady() const noexcept {
    return impl_ != nullptr && impl_->state.gpio5_ready != 0u;
}

Status KtpMobileFModule::AdvanceTime(std::uint64_t delta_microseconds) noexcept {
    if (impl_ == nullptr) {
        return Status::InvalidState;
    }
    State& state = impl_->state;
    if (delta_microseconds >
        std::numeric_limits<std::uint64_t>::max() -
            state.deterministic_time_us) {
        return Status::TimeOverflow;
    }
    state.deterministic_time_us += delta_microseconds;
    return Status::Ok;
}

void KtpMobileFModule::CaptureState(State& out) const noexcept {
    if (impl_ == nullptr) {
        /* Construct directly in the caller's storage.  `out = State{}` makes
           MSVC reserve a ~1.8 MiB temporary in every call, exceeding CERF's
           Win32 stack, while memset triggers GCC's class-memaccess warning. */
        out.~State();
        ::new (static_cast<void*>(&out)) State();
        return;
    }
    out = impl_->state;
}

Status KtpMobileFModule::RestoreState(const State& snapshot) noexcept {
    if (impl_ == nullptr) {
        return Status::InvalidState;
    }
    if (snapshot.schema_version != kStateSchemaVersion) {
        return Status::StateVersionMismatch;
    }
    if (!ValidateSnapshot(snapshot)) {
        return Status::InvalidSnapshot;
    }
    impl_->state = snapshot;
    return Status::Ok;
}

ModulePhase KtpMobileFModule::Phase() const noexcept {
    return impl_ != nullptr ? impl_->state.module_phase : ModulePhase::Startup;
}

UpdatePhase KtpMobileFModule::FirmwareUpdatePhase() const noexcept {
    return impl_ != nullptr ? impl_->state.update_phase : UpdatePhase::Inactive;
}

FirmwareInfo KtpMobileFModule::InstalledFirmware() const noexcept {
    return impl_ != nullptr ? impl_->state.firmware : FirmwareInfo{};
}

bool KtpMobileFModule::FaultActive() const noexcept {
    return impl_ != nullptr && impl_->state.fault_active != 0u;
}

}  // namespace ktp_mobile
