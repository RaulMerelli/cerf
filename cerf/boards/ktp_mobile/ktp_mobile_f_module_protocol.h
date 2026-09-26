#pragma once

#include "ktp_mobile_f_module_model.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ktp_mobile::detail {

inline constexpr std::uint8_t kPanelMarker = 0xABu;
inline constexpr std::uint8_t kModuleMarker = 0x5Fu;
inline constexpr std::uint8_t kStatusBase = 0x80u;
inline constexpr std::uint8_t kStartupRequestBit = 0x02u;
inline constexpr std::uint8_t kStartupAckBit = 0x04u;
inline constexpr std::size_t kOuterSequenceOffset = 0u;
inline constexpr std::size_t kCyclicOffset = 2u;
inline constexpr std::size_t kStatusOffset = 12u;
inline constexpr std::size_t kOuterCrcOffset = 13u;
inline constexpr std::size_t kRelayOffset = 15u;
inline constexpr std::size_t kRelayHeaderBytes = 6u;
inline constexpr std::size_t kRelayCrcBytes = 2u;
inline constexpr std::size_t kRecordHeaderBytes = 6u;
inline constexpr std::array<std::uint8_t, 6> kUpdateTarget{{'K', 'O', 'M', 'P', '_', '2'}};

struct CommandSchema {
    std::uint16_t command;
    std::uint32_t payload_length;
};

/* hmi_ktp400_mobile_v13 FModuleService.dll sub_EF1F79F8 and sub_EF1F5E34 @ 0xEF1F5E34 take
   command 240 with at most 54 bytes; when payload[0] is 0xFD and payload[1] is 0x01,
   sub_EF1F81EC routes it to a second callback and sub_EF1F7010 parses 46 big-endian bytes. */
inline constexpr std::array<CommandSchema, 14> kHostReachableSchemas{{
    {201u, 205u}, {131u, 2u}, {132u, 7u}, {133u, 7u}, {134u, 1u},
    {135u, 1u}, {138u, 20u}, {251u, 22u}, {242u, 33u}, {256u, 1u},
    {240u, 54u}, {239u, 240u}, {128u, 14u}, {130u, 2u},
}};

inline constexpr std::array<CommandSchema, 19> kModuleSchemas{{
    {201u, 205u}, {131u, 4u}, {132u, 1u}, {133u, 7u}, {134u, 1u},
    {135u, 2u}, {136u, 20u}, {137u, 4u}, {138u, 20u}, {242u, 10u},
    {256u, 21u}, {257u, 4u}, {10238u, 4u}, {240u, 58u}, {239u, 6u},
    {0xA000u, 6u}, {0xA001u, 6u}, {0xA002u, 6u}, {0xA003u, 6u},
}};

std::uint16_t NextRelaySequence(std::uint16_t current) noexcept;
bool IsBoolByte(std::uint8_t value) noexcept;
bool IsValidResetKind(ResetKind value) noexcept;
bool IsValidModulePhase(ModulePhase value) noexcept;
bool IsValidUpdatePhase(UpdatePhase value) noexcept;
bool IsSupportedSpiFormat(const SpiTransferFormat& format) noexcept;
bool AllZero(const std::uint8_t* data, std::size_t length) noexcept;
void RecomputeReady(State& state) noexcept;
void RefreshModuleStatus(State& state) noexcept;
void ClearPhysicalTransaction(State& state) noexcept;
void ClearActiveRelay(State& state) noexcept;
void ResetRelayState(State& state) noexcept;
void PromoteStagedRelay(State& state) noexcept;
Status QueueModuleRecord(State& state, std::uint16_t command,
                         const std::uint8_t* payload,
                         std::size_t payload_length) noexcept;
bool ActiveRelayIsSuccessfulUpdateResponse(const State& state) noexcept;
bool ValidateActiveRelay(const State& state) noexcept;
bool ValidateStagedRecords(const State& state) noexcept;
Status QueueInstalledVersion(State& state) noexcept;
void BuildResponseFrame(const State& state,
                        std::array<std::uint8_t, kWireTransactionBytes>& out) noexcept;
bool ValidateRecordArea(const std::uint8_t* records, std::size_t record_bytes,
                        bool host_to_module, bool allow_update_command) noexcept;
bool ValidateFirmwareInfo(const FirmwareInfo& info) noexcept;
Status DispatchIncomingRecords(State& state, const std::uint8_t* relay) noexcept;

}
