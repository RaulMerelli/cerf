#include "siemens_mp377_sm501.h"
#include "siemens_mp377_sm501_fb.h"
#include "siemens_mp377_sm501_internal.h"
#include "siemens_mp377_sm501_video.h"

#include "../../boards/board_context.h"
#include "../../core/cerf_emulator.h"

namespace siemens_mp377 {

bool SiemensMp377Sm501Video::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoard() == Board::SiemensMP377;
}

const uint8_t* SiemensMp377Sm501Video::Vram() {
    return emu_.Get<SiemensMp377Sm501Fb>().Vram();
}

bool SiemensMp377Sm501Video::WasWritten() {
    return emu_.Get<SiemensMp377Sm501Fb>().WasWritten();
}

bool SiemensMp377Sm501Video::WriteVramByte(uint32_t off, uint8_t value) {
    return emu_.Get<SiemensMp377Sm501Fb>().WriteVramByte(off, value);
}

bool SiemensMp377Sm501Video::WriteVramHalf(uint32_t off, uint16_t value) {
    return emu_.Get<SiemensMp377Sm501Fb>().WriteVramHalf(off, value);
}

bool SiemensMp377Sm501Video::WriteVramWord(uint32_t off, uint32_t value) {
    return emu_.Get<SiemensMp377Sm501Fb>().WriteVramWord(off, value);
}

uint32_t SiemensMp377Sm501Video::PanelFbOffset() {
    return emu_.Get<SiemensMp377Sm501Regs>().PanelFbOffset();
}

uint32_t SiemensMp377Sm501Video::PanelWidth() {
    return emu_.Get<SiemensMp377Sm501Regs>().PanelWidthPixels();
}

uint32_t SiemensMp377Sm501Video::PanelHeight() {
    return emu_.Get<SiemensMp377Sm501Regs>().PanelHeightLines();
}

uint32_t SiemensMp377Sm501Video::PanelPitchBytes() {
    return emu_.Get<SiemensMp377Sm501Regs>().PanelPitchBytes();
}

REGISTER_SERVICE(SiemensMp377Sm501Video);

} // namespace siemens_mp377
