#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace ktp_mobile {

inline constexpr std::size_t kWireTransactionBytes = 272;
inline constexpr std::size_t kLogicalFrameBytes = 271;
inline constexpr std::size_t kRelayAreaBytes = 256;
inline constexpr std::size_t kRelayRecordBytes = 248;
inline constexpr std::size_t kFirmwareVersionBytes = 20;
inline constexpr std::size_t kFirmwareUpdateRequestBytes = 240;
inline constexpr std::size_t kFirmwareUpdateResponseBytes = 6;
inline constexpr std::size_t kFirmwareUpdateBlockBytes = 232;
inline constexpr std::size_t kFullFlashBytes = 1024u * 1024u;
inline constexpr std::size_t kApplicationFlashOffset = 0x20000u;
inline constexpr std::size_t kApplicationFlashBytes =
    kFullFlashBytes - kApplicationFlashOffset;
inline constexpr std::size_t kUpdateContainerPrefixBytes = 0x7Cu;
inline constexpr std::size_t kMaxUpdateContainerBytes =
    kUpdateContainerPrefixBytes + kApplicationFlashBytes;

/* hmi_ktp400_mobile_v13 FModuleService.dll sub_EF1F9534 returns 271 and the SPI
   transaction is (271 + 4) >> 2 words. */
static_assert(kWireTransactionBytes == ((kLogicalFrameBytes + 4u) / 4u) * 4u);
/* hmi_ktp400_mobile_v13 FModuleFirmwareUpdater.exe sub_14A7C @ VA 0x00014A7C: an 8-byte
   header ahead of each block. */
static_assert(kFirmwareUpdateRequestBytes == kFirmwareUpdateBlockBytes + 8u);

enum class Status : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidState,
    UnsupportedSpiFormat,
    TransferWouldOverflow,
    IncompleteTransaction,
    ProtocolRejected,
    QueueFull,
    InvalidSnapshot,
};

enum class ResetKind : std::uint8_t {
    Cold = 0,
    WarmModule,
};

enum class ModulePhase : std::uint8_t {
    Startup = 0,
    Service,
    Bootloader,
};

enum class UpdatePhase : std::uint8_t {
    Inactive = 0,
    EntryRequested,
    TargetAccepted,
    Receiving,
    Finalizing,
    Complete,
    Aborted,
};

enum class SpiClockPolarity : std::uint8_t {
    IdleLow = 0,
    IdleHigh,
};

enum class SpiClockPhase : std::uint8_t {
    CaptureFirstEdge = 0,
    CaptureSecondEdge,
};

enum class SpiBitOrder : std::uint8_t {
    MsbFirst = 0,
    LsbFirst,
};

enum class SpiByteOrder : std::uint8_t {
    MostSignificantByteFirst = 0,
    LeastSignificantByteFirst,
};

struct SpiTransferFormat {
    std::uint8_t bits_per_word = 8u;
    SpiClockPolarity clock_polarity = SpiClockPolarity::IdleLow;
    SpiClockPhase clock_phase = SpiClockPhase::CaptureFirstEdge;
    SpiBitOrder bit_order = SpiBitOrder::MsbFirst;
    SpiByteOrder byte_order = SpiByteOrder::MostSignificantByteFirst;
};

struct SpiTransferResult {
    Status status = Status::Ok;
    std::size_t bytes_transferred = 0;
};

struct FirmwareInfo {
    std::uint8_t valid = 0;
    std::uint8_t flash_materialized = 0;
    std::uint16_t reserved = 0;
    std::uint32_t container_size = 0;
    std::uint32_t payload_size = 0;
    std::array<std::uint8_t, kFirmwareVersionBytes> version{};
};

struct State {
    ResetKind last_reset = ResetKind::Cold;
    ModulePhase module_phase = ModulePhase::Startup;
    UpdatePhase update_phase = UpdatePhase::Inactive;

    std::uint8_t gpio5_ready = 0;
    std::uint8_t gpio6_ack = 0;
    std::uint8_t chip_select_asserted = 0;
    std::uint8_t startup_exchange_pending = 0;

    std::uint16_t spi_bytes_transferred = 0;
    std::uint16_t reserved_spi = 0;
    std::array<std::uint8_t, kWireTransactionBytes> spi_rx{};
    std::array<std::uint8_t, kWireTransactionBytes> spi_tx{};

    std::array<std::uint8_t, 10> panel_cyclic_bytes{};
    std::array<std::uint8_t, 10> module_cyclic_bytes{};
    std::uint8_t panel_status_byte = 0;
    std::uint8_t module_status_byte = 0;
    std::uint8_t startup_control_acknowledged = 0;
    std::uint8_t reserved_outer = 0;

    std::uint16_t next_module_relay_sequence = 1;
    std::uint16_t last_panel_relay_sequence = 0;
    std::uint16_t active_module_relay_sequence = 0;
    std::uint16_t active_module_relay_length = 0;
    std::array<std::uint8_t, kRelayAreaBytes> active_module_relay{};

    std::uint16_t staged_module_record_bytes = 0;
    std::uint16_t reserved_relay = 0;
    std::array<std::uint8_t, kRelayRecordBytes> staged_module_records{};

    FirmwareInfo firmware{};
    std::array<std::uint8_t, kApplicationFlashBytes> application_flash{};

    std::uint8_t approved_container_valid = 0;
    std::array<std::uint8_t, 3> reserved_approved{};
    std::array<std::uint8_t, 32> approved_container_sha256{};

    std::uint32_t update_expected_sequence = 0;
    std::uint32_t update_staging_size = 0;
    std::uint8_t update_last_wire_status = 0;
    std::uint8_t update_final_seen = 0;
    std::array<std::uint8_t, 6> update_target{};
    std::array<std::uint8_t, kMaxUpdateContainerBytes> update_staging{};
};

static_assert(std::is_trivially_copyable<State>::value,
              "State must remain trivially copyable");
static_assert(std::is_standard_layout<State>::value,
              "State must remain standard-layout");

class KtpMobileFModule final {
public:
    KtpMobileFModule();
    ~KtpMobileFModule();

    KtpMobileFModule(const KtpMobileFModule&) = delete;
    KtpMobileFModule& operator=(const KtpMobileFModule&) = delete;

    void ColdReset() noexcept;
    void WarmModuleReset() noexcept;

    Status ConfigureFirmwareContainer(const std::uint8_t* container,
                                      std::size_t length,
                                      bool install) noexcept;

    Status SetChipSelect(bool asserted) noexcept;

    SpiTransferResult TransferSpi(const std::uint8_t* panel_tx,
                                  std::uint8_t* panel_rx,
                                  std::size_t byte_count,
                                  SpiTransferFormat format) noexcept;

    Status SetPanelGpio6(bool high) noexcept;

    bool ModuleGpio5DataReady() const noexcept;

    void CaptureState(State& out) const noexcept;
    Status RestoreState(const State& snapshot) noexcept;

    FirmwareInfo InstalledFirmware() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
