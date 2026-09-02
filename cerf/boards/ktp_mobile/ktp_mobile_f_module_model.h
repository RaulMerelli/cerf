#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace ktp_mobile {

/* Evidence-backed wire/storage limits from Step 1. */
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
inline constexpr std::uint32_t kStateSchemaVersion = 2u;

enum class Status : std::uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidState,
    UnsupportedSpiFormat,
    TransferWouldOverflow,
    IncompleteTransaction,
    ProtocolRejected,
    QueueFull,
    StateVersionMismatch,
    InvalidSnapshot,
    TimeOverflow,
};

enum class ResetKind : std::uint8_t {
    Cold = 0,
    WarmModule,
};

enum class ModulePhase : std::uint8_t {
    Startup = 0,
    Service,
    Fault,
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

/*
 * Plain semantic snapshot. All fields are pointer-free and use fixed-width
 * integers / fixed-size arrays. Callers must serialize fields explicitly;
 * raw-dumping sizeof(State) is not a cross-compiler file format because C++
 * structure padding is implementation-defined.
 */
struct State {
    std::uint32_t schema_version = kStateSchemaVersion;

    ResetKind last_reset = ResetKind::Cold;
    ModulePhase module_phase = ModulePhase::Startup;
    UpdatePhase update_phase = UpdatePhase::Inactive;

    std::uint8_t gpio5_ready = 0;
    std::uint8_t gpio6_ack = 0;
    std::uint8_t chip_select_asserted = 0;
    std::uint8_t startup_exchange_pending = 0;

    std::uint64_t deterministic_time_us = 0;

    /* A CS-bounded transfer may be snapshotted at any byte boundary. */
    std::uint16_t spi_bytes_transferred = 0;
    std::uint16_t reserved_spi = 0;
    std::array<std::uint8_t, kWireTransactionBytes> spi_rx{};
    std::array<std::uint8_t, kWireTransactionBytes> spi_tx{};

    /* Raw outer-frame state. No unproven health semantics are attached. */
    std::array<std::uint8_t, 10> panel_cyclic_bytes{};
    std::array<std::uint8_t, 10> module_cyclic_bytes{};
    std::uint8_t panel_status_byte = 0;
    std::uint8_t module_status_byte = 0;
    std::uint8_t startup_control_acknowledged = 0;
    std::uint8_t reserved_outer = 0;

    /* Module -> panel relay currently advertised until acknowledged. */
    std::uint16_t next_module_relay_sequence = 1;
    std::uint16_t last_panel_relay_sequence = 0;
    std::uint16_t active_module_relay_sequence = 0;
    std::uint16_t active_module_relay_length = 0;
    std::array<std::uint8_t, kRelayAreaBytes> active_module_relay{};

    /* Record bytes waiting to become the next relay; max is protocol 248. */
    std::uint16_t staged_module_record_bytes = 0;
    std::uint16_t reserved_relay = 0;
    std::array<std::uint8_t, kRelayRecordBytes> staged_module_records{};

    /* Explicit module-reported fault state; command 240 uses 58 wire bytes. */
    std::uint8_t fault_active = 0;
    std::uint8_t reserved_fault0 = 0;
    std::uint16_t fault_payload_size = 0;
    std::array<std::uint8_t, 58> fault_payload{};

    /* Persistent installed application state. Reset never erases this array. */
    FirmwareInfo firmware{};
    std::array<std::uint8_t, kApplicationFlashBytes> application_flash{};

    /* Exact FWF-supplied update image accepted by the native updater. */
    std::uint8_t approved_container_valid = 0;
    std::array<std::uint8_t, 3> reserved_approved{};
    std::array<std::uint8_t, 32> approved_container_sha256{};

    /* Volatile update session / complete incoming container staging. */
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
    KtpMobileFModule(KtpMobileFModule&&) noexcept;
    KtpMobileFModule& operator=(KtpMobileFModule&&) noexcept;

    /* Reset the volatile MCU/protocol state. Installed Flash/version persist. */
    void ColdReset() noexcept;
    void WarmModuleReset() noexcept;

    /* Register the exact update container supplied by this panel's fixed FWF.
       When install is true, materialize its application payload as the
       firmware fitted to a newly-created emulated panel. */
    Status ConfigureFirmwareContainer(const std::uint8_t* container,
                                      std::size_t length,
                                      bool install) noexcept;

    /*
     * Chip-select is an explicit transaction boundary. Assertion snapshots the
     * response frame to be shifted out. Deassertion commits only a complete
     * 272-byte transaction; a short transaction is aborted deterministically.
     */
    Status SetChipSelect(bool asserted) noexcept;

    /*
     * Full-duplex logical SPI byte stream. The CERF adapter must unpack eCSPI
     * FIFO words before calling this method. The evidence-backed device accepts
     * only 8-bit, MSB-first, mode-0 traffic. The adapter supplies the bus
     * configuration explicitly; eCSPI FIFO packing remains adapter-side.
     */
    SpiTransferResult TransferSpi(const std::uint8_t* panel_tx,
                                  std::uint8_t* panel_rx,
                                  std::size_t byte_count,
                                  SpiTransferFormat format) noexcept;

    /* Panel -> module GPIO5_IO06 ACK/CONTROL. */
    Status SetPanelGpio6(bool high) noexcept;

    /* Module -> panel GPIO5_IO05 READY. */
    bool ModuleGpio5DataReady() const noexcept;

    /* Deterministic model time; never consults a host wall clock. */
    Status AdvanceTime(std::uint64_t delta_microseconds) noexcept;

    /* Complete capture/restore, including an in-flight SPI or update transfer. */
    void CaptureState(State& out) const noexcept;
    Status RestoreState(const State& snapshot) noexcept;

    ModulePhase Phase() const noexcept;
    UpdatePhase FirmwareUpdatePhase() const noexcept;
    FirmwareInfo InstalledFirmware() const noexcept;
    bool FaultActive() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ktp_mobile
