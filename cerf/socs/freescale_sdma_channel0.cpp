#include "freescale_sdma_channel0.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../state/state_stream.h"

#include <cstring>

using namespace cerf_freescale_sdma_detail;

REGISTER_SERVICE(FreescaleSdmaChannel0);

bool FreescaleSdmaChannel0::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    if (!board) return false;
    const SocFamily soc = board->GetSoc();
    return soc == SocFamily::iMX31 || soc == SocFamily::iMX51 ||
           soc == SocFamily::iMX6;
}

void FreescaleSdmaChannel0::OnReady() {
    Reset();
}

void FreescaleSdmaChannel0::SaveState(StateWriter& writer) const {
    writer.WriteBytes(context_loaded_, sizeof(context_loaded_));
    writer.Write(current_address_);
    writer.WriteBytes(program_, sizeof(program_));
    writer.WriteBytes(data_, sizeof(data_));
}

void FreescaleSdmaChannel0::RestoreState(StateReader& reader) {
    reader.ReadBytes(context_loaded_, sizeof(context_loaded_));
    reader.Read(current_address_);
    reader.ReadBytes(program_, sizeof(program_));
    reader.ReadBytes(data_, sizeof(data_));
}

void FreescaleSdmaChannel0::Reset() {
    std::memset(context_loaded_, 0, sizeof(context_loaded_));
    current_address_ = 0;
    std::memset(program_, 0, sizeof(program_));
    std::memset(data_, 0, sizeof(data_));
}

void FreescaleSdmaChannel0::Execute(uint32_t mode, uint32_t arm_src_pa,
                                    uint32_t sdma_dst_word,
                                    uint32_t sdma_mmio_base) {
    const uint32_t count = mode & 0xFFFFu;
    const uint32_t command = (mode >> 24) & 0xFFu;
    const uint32_t base_command = command & 0x07u;
    auto& memory = emu_.Get<EmulatedMemory>();

    switch (base_command) {
        case kC0SetPm: {
            uint32_t copied = 0;
            for (; copied < count &&
                   (sdma_dst_word + copied) < kSdmaProgramWords; ++copied) {
                uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 2u);
                if (!src) break;
                uint16_t value = 0;
                std::memcpy(&value, src, sizeof(value));
                program_[sdma_dst_word + copied] = value;
            }
            current_address_ = sdma_dst_word + copied;
            return;
        }
        case kC0SetDm: {
            uint32_t copied = 0;
            for (; copied < count &&
                   (sdma_dst_word + copied) < kSdmaDataWords; ++copied) {
                uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 4u);
                if (!src) break;
                uint32_t value = 0;
                std::memcpy(&value, src, sizeof(value));
                data_[sdma_dst_word + copied] = value;
            }
            current_address_ = sdma_dst_word + copied;
            return;
        }
        case kC0SetCtx: {
            const uint32_t channel = command >> 3;
            const uint32_t destination = kSdmaContextBase + count * channel;
            uint32_t copied = 0;
            for (; copied < count &&
                   (destination + copied) < kSdmaDataWords; ++copied) {
                uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 4u);
                if (!src) break;
                uint32_t value = 0;
                std::memcpy(&value, src, sizeof(value));
                data_[destination + copied] = value;
            }
            if (channel < kChannelCount)
                context_loaded_[channel] = copied == count;
            return;
        }
        default:
            LOG(Caution,
                "Freescale SDMA: unsupported channel-0 command "
                "HSTART=0x%08X mode=0x%08X\n",
                sdma_mmio_base + kOffHstart, mode);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}
