#include "freescale_sdma_channel0.h"
#include "../core/fatal.h"

#include "../boards/board_context.h"
#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../state/state_stream.h"

#include <cstring>
#include "imx6/imx6_id.h"
#include "imx51/imx51_id.h"
#include "imx31/imx31_id.h"

using namespace cerf_freescale_sdma_detail;

REGISTER_SERVICE(FreescaleSdmaChannel0);

bool FreescaleSdmaChannel0::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    if (!board) return false;
    const std::string_view soc = board->GetSocId();
    return soc == SocId::Imx31 || soc == SocId::Imx51 || soc == SocId::Imx6;
}

void FreescaleSdmaChannel0::OnReady() {
    Reset();
}

void FreescaleSdmaChannel0::SaveState(StateWriter& writer) const {
    writer.Write("current_address", current_address_);
    writer.WriteBytes("program", program_, sizeof(program_));
    writer.WriteBytes("data", data_, sizeof(data_));
}

void FreescaleSdmaChannel0::RestoreState(StateReader& reader) {
    reader.Read("current_address", current_address_);
    reader.ReadBytes("program", program_, sizeof(program_));
    reader.ReadBytes("data", data_, sizeof(data_));
}

void FreescaleSdmaChannel0::Reset() {
    current_address_ = 0;
    std::memset(program_, 0, sizeof(program_));
    std::memset(data_, 0, sizeof(data_));
}

void FreescaleSdmaChannel0::Execute(uint32_t mode, uint32_t arm_src_pa, uint32_t sdma_dst_word,
                                    uint32_t sdma_mmio_base) {
    const uint32_t count = mode & 0xFFFFu;
    const uint32_t command = (mode >> 24) & 0xFFu;
    const uint32_t base_command = command & 0x07u;
    auto& memory = emu_.Get<EmulatedMemory>();

    switch (base_command) {
    case kC0SetPm: {
        uint32_t copied = 0;
        for (; copied < count && (sdma_dst_word + copied) < kSdmaProgramWords; ++copied) {
            uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 2u);
            if (!src) emu_.Get<Fatal>().Die("[SDMA] channel 0 program source 0x%08X is not mapped", arm_src_pa + copied * 2u);
            uint16_t value = 0;
            std::memcpy(&value, src, sizeof(value));
            program_[sdma_dst_word + copied] = value;
        }
        current_address_ = sdma_dst_word + copied;
        return;
    }
    case kC0SetDm: {
        uint32_t copied = 0;
        for (; copied < count && (sdma_dst_word + copied) < kSdmaDataWords; ++copied) {
            uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 4u);
            if (!src) emu_.Get<Fatal>().Die("[SDMA] channel 0 data source 0x%08X is not mapped", arm_src_pa + copied * 4u);
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
        for (; copied < count && (destination + copied) < kSdmaDataWords; ++copied) {
            uint8_t* src = memory.TryTranslate(arm_src_pa + copied * 4u);
            if (!src) emu_.Get<Fatal>().Die("[SDMA] channel 0 context source 0x%08X is not mapped", arm_src_pa + copied * 4u);
            uint32_t value = 0;
            std::memcpy(&value, src, sizeof(value));
            data_[destination + copied] = value;
        }
        return;
    }
    default:
        emu_.Get<Fatal>().Die("Freescale SDMA: unsupported channel-0 command "
                              "HSTART=0x%08X mode=0x%08X",
                              sdma_mmio_base + kOffHstart, mode);
    }
}
