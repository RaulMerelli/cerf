#pragma once

#include "../../peripherals/peripheral_base.h"

#include <cstdint>
#include <vector>

/* Casio companion ASIC in External I/O area 2 (IOCS0#), PA 0x0A000000 (kseg1
   0xAA000000; VR4131 UM U15350EJ2V0UM Fig 3-1 p75); registers from
   nk_main_kernel.exe sub_9F032B60, sub_9F0389EC, sub_9F03445C, sub_9F0346B0,
   and ddi.dll sub_FC5538/sub_FC4E38. */
class CasioCassiopeiaEm500Companion : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    uint32_t ReadWord (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    /* nk_main_kernel.exe sub_9F03445C @0x9F034484 (1 -> 0x980), power-down
       @0x9F038B50 (0 -> 0x980); ddi.dll sub_FC3E00 @0xFC3E00. */
    bool IsDisplayEnabled() const { return reg_0980_ == 1u; }

    /* nk_main_kernel.exe fill sub_9F0346B0: 0x1E0 bytes/row, row advance 0x200,
       extent 0x28000 from 0xAA200000; boot-logo draw @0x9F034408-0x9F034410
       (dst 0xAA200000, w=0xF0, h=0x140); ddi.dll sub_FC5538 VirtualCopy
       0xAA200000 size 0x40000. */
    uint32_t GuestW()      const { return 240u; }
    uint32_t GuestH()      const { return 320u; }
    uint32_t StrideBytes() const { return 512u; }
    uint32_t FbPa()        const { return kBase + kFbOffset; }
    uint32_t FbSize()      const { return kFbSize; }
    const uint8_t* FbBytes() const { return fb_.data(); }

private:
    static constexpr uint32_t kBase       = 0x0A000000u;
    static constexpr uint32_t kWindowSize = 0x00240000u;
    static constexpr uint32_t kFbOffset   = 0x00200000u;
    static constexpr uint32_t kFbSize     = 0x00040000u;

    bool InFb(uint32_t off) const { return off >= kFbOffset && off < kFbOffset + kFbSize; }
    void WriteReg(uint32_t off, uint32_t value);
    void RunBlit();
    void MaybePublishDisplaySize();

    std::vector<uint8_t> fb_;

    /* nk_main_kernel.exe sub_9F03C104 @0x9F03C120, sub_9F03C140 @0x9F03C160,
       @0x9F033174/@0x9F0331B8. */
    uint32_t mbox_cmd_ = 0;
    /* nk_main_kernel.exe sub_9F0389EC @0x9F0389EC */
    uint32_t ctrl8904_ = 0;
    /* nk_main_kernel.exe @0x9F0331FC-0x9F033284 (bit0/bit1 RMW pulses). */
    uint32_t ctrl_a0d4_ = 0;
    /* nk_main_kernel.exe @0x9F03C188-0x9F03C194; sub_9F08EE0C SYSINTR-19/28/30/31. */
    uint32_t clk8004_ = 0;
    /* nk_main_kernel.exe sub_9F038A58 @0x9F038A60 */
    uint16_t data_a040_ = 0;
    /* nk_main_kernel.exe @0x9F032CDC, sub_9F03473C @0x9F034834/@0x9F034844. */
    uint32_t reg_1110_ = 0;
    /* pcmcia.dll @0xF81878/@0xF81880 (RMW |0x30). */
    uint32_t socket_ctrl_ac8_ = 0;
    /* pcmcia.dll @0xF81D76/@0xF81D80 (RMW &~0xC). */
    uint32_t socket_a038_ = 0;
    /* nk_main_kernel.exe sub_9F08EE0C @0x9F08EE28/@0x9F08EE34. */
    uint16_t intcfg8404_ = 0;
    /* wavedev.dll sub_F616F4 @0xF61708, loc_F61EAC @0xF61EC8-0xF61F4C,
       sub_F62520 @0xF62550-0xF62596; 0x1118 read @0xF61822-0xF6182E. */
    uint32_t codec880_  = 0;
    uint32_t codec884_  = 0;
    uint32_t codec888_  = 0;
    uint32_t codec890_  = 0;
    uint32_t codec898_  = 0;
    uint32_t codec8A0_  = 0;
    uint32_t latch1118_ = 0;
    /* nk_main_kernel.exe @0x9F035930 (sw 0x10), idle @0x9F0388F4 (lw; andi 1). */
    uint32_t latch130C_ = 0;
    /* socket.dll @0xF41978-0xF41984 (0x4000 -> table[4]=+0 @0xF421D2, desc
       0xAA008000 @0xF421F8) via sub_F42258 store. */
    uint32_t strap8000_ = 0;
    /* remocon.dll mapper @0xED181C-0xED1820 (base+0x89C); init RMW @0xED158C
       (&~0x38); IST @0xED15D6-0xED15E4 (&~0x30|8), @0xED1682-0xED168C (&~0x38);
       gate sub_ED19A0 (&0x40). */
    uint32_t adc_ctrl_89C_ = 0;
    /* nk_main_kernel.exe @0x9F033118/@0x9F033130 (0x0910), @0x9F0331D8-0x9F0331F0
       (0x0900/0x0908/0x090C). */
    uint32_t sib_regs_[5] = {};
    /* nk_main_kernel.exe sub_9F038AF0 @0x9F038AF8 */
    uint32_t bus_timing_[16] = {};
    /* nk_main_kernel.exe @0x9F0330DC-0x9F0330F0; keybddr.dll sub_FB1F80
       base+(0x30+i)*2, i=0..15. */
    uint16_t edge_cfg_[16] = {};

    /* nk_main_kernel.exe sub_9F03445C @0x9F03446C-0x9F034484 (0x984=1, 0x988=0x1B7,
       0x98C=0x1B7, 0x980=1), power-down @0x9F038B48-0x9F038B58 (0x980=0, 0x994=0);
       ddi.dll sub_FC2374 @0xFC2374 (0x98C brightness) / @0xFC276C (0x99C contrast). */
    uint32_t reg_0980_ = 0;
    uint32_t reg_0984_ = 0;
    uint32_t reg_0988_ = 0;
    uint32_t reg_098C_ = 0;
    uint32_t reg_0994_ = 0;
    uint32_t reg_099C_ = 0;

    /* ddi.dll sub_FC4E38 @0xFC4E38: [0xA00] go/busy (poll 0, write 1; cleared on
       done), [0xA04] opcode 0x81, [0xA08] length in words, [0xA10] source kseg1
       address (0xA000D000 + offset), [0xA14] destination framebuffer byte offset. */
    uint32_t blit_op_        = 0;
    uint32_t blit_len_words_ = 0;
    uint32_t blit_src_       = 0;
    uint32_t blit_dst_       = 0;

    bool size_published_ = false;
};
