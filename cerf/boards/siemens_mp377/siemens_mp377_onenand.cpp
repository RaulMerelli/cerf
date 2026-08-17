#include "../../peripherals/peripheral_base.h"

#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../state/state_stream.h"
#include "../board_context.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

/* Samsung OneNAND flash model for the Siemens MP 377, mapped onto the
   IOP13xx secondary ATU outbound flash aperture at PA 0xD0200000..0xD027FFFF.
   The P377/TFFS path reaches the same chip window through at least two ATU
   aliases: data/spare RAM at 0xD0200000 and the historical CERF chip base at
   0xD0240000. Both aliases decode to the same 256 KB OneNAND register/RAM
   layout below. The TFFS3.dll OneNAND driver (sub_2BD2C14 et al.) drives a
   16-bit-word register file reached through a 2× host byte-stride; chip word
   0xFXXX corresponds to logical host offset 0x3CXXX. Register layout (decompiled from sub_2BD27B0
   "OneNand_DumpRegisters" + sub_2BD2C14):

     +0x0000..0x1FFFF Buffer/data RAM window used by TFFS
     +0x3C000         Manufacturer ID                  R
     +0x3C004         Device ID                        R
     +0x3C008         Version ID                       R
     +0x3C00C         Data buffer size                 R
     +0x3C010         Boot buffer size                 R
     +0x3C018         Technology                       R
     +0x3C400         Start Address 1 (block + page)   RW
     +0x3C404         Start Address 2                  RW
     +0x3C408         Start Address 3                  RW
     +0x3C40C         Start Address 4                  RW
     +0x3C880         Command register                 W
     +0x3C8C0         System config 1                  RW
     +0x3C900         Controller error status          R
     +0x3C904         Interrupt status (completion)    RW
     +0x3C908..0x3C97F TFFS status/ECC result/clear    RW

   Backing storage is held inline as 256 MB (1024 blocks × 64 pages × 4 KB
   main). The lower 128 KB logical chip window is modelled as RAM because TFFS
   clears and stages format buffers beyond the two documented data buffers
   (observed at logical offset 0x2800). The logical spare-RAM aperture at
   0x20000 is modelled too; TFFS reaches it through the low alias at
   PA 0xD0220000. Spare area (BBT / ECC bytes) is held as a separate 1024 × 64 ×
   128-byte region; all bytes start at 0xFF (erased pattern). The FSM serves TFFS3
   well enough for the partition scanner to format an empty volume so the
   guest filesys.exe can then spawn device.exe / gwes.exe. */

namespace {

constexpr size_t kPageMain    = 4096;
constexpr size_t kPageSpare   = 128;
constexpr size_t kPagesBlock  = 64;
constexpr size_t kStorageBlocks = 1024;
constexpr size_t kBackingSize = kStorageBlocks * kPagesBlock * kPageMain;
constexpr size_t kSpareSize   = kStorageBlocks * kPagesBlock * kPageSpare;

constexpr uint32_t kDataRam0       = 0x0800u;
constexpr uint32_t kDataRam1       = 0x1800u;
constexpr uint32_t kDataRamEnd     = kDataRam1 + static_cast<uint32_t>(kPageMain);
constexpr uint32_t kBufferRamEnd   = 0x20000u;
constexpr uint32_t kSpareRamBase   = 0x20000u;
constexpr uint32_t kSpareRamEnd  = kSpareRamBase + static_cast<uint32_t>(kPageSpare);
constexpr uint32_t kBootStateMirrorBase = 0x3FF00u;
constexpr uint32_t kBootStateMirrorEnd  = 0x40000u;
constexpr size_t   kBootStateMirrorSize = kBootStateMirrorEnd - kBootStateMirrorBase;

class SiemensMp377OneNand : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoard() == Board::SiemensMP377;
    }
    void OnReady() override {
        backing_.assign(kBackingSize, uint8_t{0xFFu});
        spare_.assign(kSpareSize,    uint8_t{0xFFu});
        data_ram_.fill(uint8_t{0xFFu});
        spare_ram_.fill(uint8_t{0xFFu});
        boot_state_mirror_.fill(uint8_t{0xFFu});
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0xD0200000u; }
    /* 512 KB aperture: two 256 KB aliases of the same OneNAND chip window. */
    uint32_t MmioSize() const override { return 0x00080000u; }

    uint8_t  ReadByte(uint32_t addr) override {
        const uint32_t off = DecodeOffset(addr);
        if (off < kBufferRamEnd) return data_ram_[off];
        if (off >= kSpareRamBase && off < kSpareRamEnd) return spare_ram_[off - kSpareRamBase];
        if (off >= kBootStateMirrorBase && off < kBootStateMirrorEnd) return boot_state_mirror_[off - kBootStateMirrorBase];
        const uint16_t h = ReadHalf(addr & ~uint32_t{1});
        return static_cast<uint8_t>((addr & 1u) ? (h >> 8) : h);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = DecodeOffset(addr);
        if (off < kBufferRamEnd) {
            const size_t i = off & ~uint32_t{1};
            return static_cast<uint16_t>(data_ram_[i] | (data_ram_[i + 1] << 8));
        }
        if (off >= kSpareRamBase && off < kSpareRamEnd) {
            const size_t i = (off - kSpareRamBase) & ~uint32_t{1};
            return static_cast<uint16_t>(spare_ram_[i] | (spare_ram_[i + 1] << 8));
        }
        if (off >= kBootStateMirrorBase && off < kBootStateMirrorEnd) {
            const size_t i = (off - kBootStateMirrorBase) & ~uint32_t{1};
            return static_cast<uint16_t>(boot_state_mirror_[i] | (boot_state_mirror_[i + 1] << 8));
        }
        /* ECC Status Registers. sub_2BD2740 does
             ADD R3, R3, #0x3FC00 ; LDR LR, [R3]
           on the chip base — i.e. a 32-bit read at host byte 0x3FC00,
           which corresponds to chip word 0xFF00 (Samsung OneNAND ECC
           Status Register 0) at a 4-byte host stride. It then loops over
           8 two-bit sector slots; any nonzero slot reports "1bit error".
           Returning 0 across the ECC register page silences that flood
           — the unmapped-area 0xFFFF default would mark every sector as
           failed. The legacy 0x1FE00..0x1FE0F range (2-byte stride) is
           also covered as a defensive overlay. */
        if (off >= 0x1FE00u && off < 0x1FE10u) return 0;
        if (off >= 0x3FC00u && off < 0x3FD00u) return 0;
        /* Register page: only the 0x3CXXX block carries defined registers; the
           rest of the chip-mapped window is unused buffer space that should
           read as erased flash (0xFFFF). sub_2BD3118 polls spare-area bytes at
           +0x20040/+0x20050 looking for the bad-block marker; returning 0 there
           tags every block as "initial bad block". */
        if (off >= 0x3C000u && off < 0x3D000u) return RegRead16(off);
        return 0xFFFFu;
    }
    uint32_t ReadWord(uint32_t addr) override {
        /* Word access reads two adjacent 16-bit halves (both planes). */
        const uint16_t lo = ReadHalf(addr);
        const uint16_t hi = ReadHalf(addr + 2);
        return static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
    }
    void WriteByte(uint32_t addr, uint8_t v) override {
        const uint32_t off = DecodeOffset(addr);
        if (off < kBufferRamEnd) { data_ram_[off] = v; return; }
        if (off >= kSpareRamBase && off < kSpareRamEnd) { spare_ram_[off - kSpareRamBase] = v; return; }
        if (off >= kBootStateMirrorBase && off < kBootStateMirrorEnd) { boot_state_mirror_[off - kBootStateMirrorBase] = v; return; }
        HaltUnsupportedAccess("OneNAND byte write outside RAM", addr, v);
    }
    void WriteHalf(uint32_t addr, uint16_t v) override {
        const uint32_t off = DecodeOffset(addr);
        if (off < kBufferRamEnd) {
            const size_t i = off & ~uint32_t{1};
            data_ram_[i]     = static_cast<uint8_t>(v & uint8_t{0xFFu});
            data_ram_[i + 1] = static_cast<uint8_t>((v >> 8) & uint8_t{0xFFu});
            return;
        }
        if (off >= kSpareRamBase && off < kSpareRamEnd) {
            const size_t i = (off - kSpareRamBase) & ~uint32_t{1};
            spare_ram_[i]     = static_cast<uint8_t>(v & uint8_t{0xFFu});
            spare_ram_[i + 1] = static_cast<uint8_t>((v >> 8) & uint8_t{0xFFu});
            return;
        }
        if (off >= kBootStateMirrorBase && off < kBootStateMirrorEnd) {
            const size_t i = (off - kBootStateMirrorBase) & ~uint32_t{1};
            boot_state_mirror_[i]     = static_cast<uint8_t>(v & uint8_t{0xFFu});
            boot_state_mirror_[i + 1] = static_cast<uint8_t>((v >> 8) & uint8_t{0xFFu});
            return;
        }
        RegWrite16(off, v);
    }
    void WriteWord(uint32_t addr, uint32_t v) override {
        WriteHalf(addr,     static_cast<uint16_t>(v & 0xFFFFu));
        WriteHalf(addr + 2, static_cast<uint16_t>((v >> 16) & 0xFFFFu));
    }

    void SaveState(StateWriter& w) override {
        WriteVector(w, backing_);
        WriteVector(w, spare_);
        w.WriteBytes(data_ram_.data(), data_ram_.size());
        w.WriteBytes(spare_ram_.data(), spare_ram_.size());
        w.WriteBytes(boot_state_mirror_.data(), boot_state_mirror_.size());
        w.Write(start_addr_1_);
        w.Write(start_addr_2_);
        w.Write(start_addr_3_);
        w.Write(start_addr_4_);
        w.Write(start_addr_5_);
        w.Write(start_addr_6_);
        w.Write(start_addr_7_);
        w.Write(start_addr_8_);
        w.Write(start_buffer_);
        w.Write(sys_cfg_);
        w.Write(ctrl_status_);
        w.Write(buffer_count_);
        w.Write(last_cmd_);
    }

    void RestoreState(StateReader& r) override {
        ReadVector(r, backing_, kBackingSize, "OneNAND backing state size");
        ReadVector(r, spare_, kSpareSize, "OneNAND spare state size");
        r.ReadBytes(data_ram_.data(), data_ram_.size());
        r.ReadBytes(spare_ram_.data(), spare_ram_.size());
        r.ReadBytes(boot_state_mirror_.data(), boot_state_mirror_.size());
        r.Read(start_addr_1_);
        r.Read(start_addr_2_);
        r.Read(start_addr_3_);
        r.Read(start_addr_4_);
        r.Read(start_addr_5_);
        r.Read(start_addr_6_);
        r.Read(start_addr_7_);
        r.Read(start_addr_8_);
        r.Read(start_buffer_);
        r.Read(sys_cfg_);
        r.Read(ctrl_status_);
        r.Read(buffer_count_);
        r.Read(last_cmd_);
    }

    static constexpr uint32_t kOneNandAliasStride = 0x00040000u;

    uint32_t DecodeOffset(uint32_t addr) const {
        const uint32_t off = addr - MmioBase();
        return off >= kOneNandAliasStride ? off - kOneNandAliasStride : off;
    }

private:
    static void WriteVector(StateWriter& w, const std::vector<uint8_t>& v) {
        const uint64_t n = static_cast<uint64_t>(v.size());
        w.Write(n);
        if (n) w.WriteBytes(v.data(), static_cast<size_t>(n));
    }

    void ReadVector(StateReader& r, std::vector<uint8_t>& v, size_t expected, const char* what) {
        uint64_t n = 0;
        r.Read(n);
        if (n != static_cast<uint64_t>(expected)) {
            HaltUnsupportedAccess(what, MmioBase(), n);
        }
        v.resize(expected);
        if (expected) r.ReadBytes(v.data(), expected);
    }


    /* Page index in the backing array: linear page number computed from
       Start Address 1 (block) + Start Address 8 (page-in-block). sub_2BD2C14
       writes SA8 as ((page << 2) | 1), replicated across both halfwords. */
    size_t PageIndex() const {
        const uint16_t block_no = start_addr_1_;
        const uint16_t page_no  = static_cast<uint16_t>((start_addr_8_ >> 2) & 0x003Fu);
        return (size_t)block_no * kPagesBlock + (size_t)page_no;
    }
    size_t BlockIndex() const {
        return start_addr_1_;
    }
    uint32_t SelectedDataRamOffset() const {
        return ((start_buffer_ & 0x0FFFu) == 0x0C00u) ? kDataRam1 : kDataRam0;
    }

    void RunCommand(uint16_t cmd) {
        const uint8_t cmd8 = static_cast<uint8_t>(cmd & uint8_t{0xFFu});
        bool main_ok = true;
        bool spare_ok = true;
        switch (cmd8) {
            case 0x00:    /* Load: page main area */
            case 0x13: {  /* Load: page main + spare */
                const size_t pi = PageIndex();
                const size_t main_off  = pi * kPageMain;
                const size_t spare_off = pi * kPageSpare;
                const uint32_t ram = SelectedDataRamOffset();
                std::fill(data_ram_.begin() + ram, data_ram_.begin() + ram + kPageMain, uint8_t{0xFFu});
                spare_ram_.fill(uint8_t{0xFFu});
                main_ok = main_off + kPageMain <= backing_.size();
                spare_ok = spare_off + kPageSpare <= spare_.size();
                if (main_ok) {
                    std::memcpy(&data_ram_[ram], &backing_[main_off], kPageMain);
                }
                if (spare_ok) {
                    std::memcpy(spare_ram_.data(), &spare_[spare_off], kPageSpare);
                }
                break;
            }
            case 0x1A: {  /* Program spare area */
                const size_t pi = PageIndex();
                const size_t spare_off = pi * kPageSpare;
                spare_ok = spare_off + kPageSpare <= spare_.size();
                main_ok = true;
                if (spare_ok) {
                    std::memcpy(&spare_[spare_off], spare_ram_.data(), kPageSpare);
                }
                break;
            }
            case 0x80: {  /* Program main area */
                const size_t pi = PageIndex();
                const size_t main_off = pi * kPageMain;
                const uint32_t ram = SelectedDataRamOffset();
                main_ok = main_off + kPageMain <= backing_.size();
                spare_ok = true;
                if (main_ok) {
                    std::memcpy(&backing_[main_off], &data_ram_[ram], kPageMain);
                }
                break;
            }
            case 0x94: {  /* Block erase  */
                const size_t bi  = BlockIndex();
                const size_t mb  = bi * kPagesBlock * kPageMain;
                const size_t sb  = bi * kPagesBlock * kPageSpare;
                const size_t mlen = kPagesBlock * kPageMain;
                const size_t slen = kPagesBlock * kPageSpare;
                main_ok = mb + mlen <= backing_.size();
                spare_ok = sb + slen <= spare_.size();
                if (main_ok) std::fill(backing_.begin() + mb, backing_.begin() + mb + mlen, uint8_t{0xFFu});
                if (spare_ok) std::fill(spare_.begin() + sb, spare_.begin() + sb + slen, uint8_t{0xFFu});
                break;
            }
            case 0x23: {  /* Write-buffer setup/unlock */
                break;
            }
            case 0x70: {  /* Read status / direct status command */
                break;
            }
            case 0xF0: {  /* Reset */
                break;
            }
            default:
                HaltUnsupportedAccess("OneNAND unknown command", MmioBase() + 0x3C880u, cmd8);
        }
        last_cmd_ = cmd8;
        ctrl_status_ = 0; /* no error */
    }

    /* sub_2BD2C14 sets expected CSR value per command and sub_2BD2B08 polls
       the chip's controller status (host byte 0x3C904 = chip word 0xF241) for
       an exact 32-bit match. Decoded from the function:
         cmd 0x00, 0x13  → 0x80808080  (page load)
         cmd 0x1A, 0x80  → 0x80408040  (block erase)
         cmd 0x94        → 0x80208020  (multi-block erase)
         default         → 0x80008000  (program 0x23, reset, etc.) */
    uint16_t CompletionLow() const {
        switch (last_cmd_) {
            case 0x00: case 0x13: return 0x8080u;
            case 0x1A: case 0x80: return 0x8040u;
            case 0x94:            return 0x8020u;
            default:              return 0x8000u;
        }
    }
    uint16_t CompletionHigh() const { return CompletionLow(); }

    static bool IsTffsStatusOrEccReg(uint32_t off) {
        /* The TFFS3 format path clears and probes a compact OneNAND
           status/ECC-result cluster after issuing command 0x23. The concrete
           accesses observed so far are F24C/F24D (host 0x3C930/0x3C934),
           with neighbouring F242/F24E already used by the scan path. Keep this
           as a small register block, not a whole 0x3Cxxx catch-all. */
        return off >= 0x3C908u && off < 0x3C980u;
    }

    static uint16_t TffsStatusOrEccRead(uint32_t off) {
        switch (off) {
            case 0x3C908u:
            case 0x3C90Au:
            case 0x3C938u:
            case 0x3C93Au:
                return 0x0004u;
            default:
                return 0x0000u;
        }
    }

    uint16_t RegRead16(uint32_t off) {
        if (IsTffsStatusOrEccReg(off)) return TffsStatusOrEccRead(off);
        switch (off) {
            case 0x3C000u: return 0x00ECu;  /* Manufacturer ID = Samsung */
            case 0x3C004u: return 0x0050u;  /* Device ID — KFG-family */
            case 0x3C008u: return 0x0030u;  /* Version ID */
            case 0x3C00Cu: return 0x0400u;  /* Data buffer size = 1 KB (one buffer) */
            case 0x3C010u: return 0x0200u;  /* Boot buffer size = 512 B */
            case 0x3C018u: return 0x0000u;  /* Technology = SLC */
            case 0x3C002u:
            case 0x3C006u:
            case 0x3C00Au:
            case 0x3C00Eu:
            case 0x3C012u:
            case 0x3C01Au: return 0x0000u;  /* high half of ID/info word */
            case 0x3C400u: return start_addr_1_;
            case 0x3C404u: return start_addr_2_;
            case 0x3C408u: return start_addr_3_;
            case 0x3C40Cu: return start_addr_4_;
            case 0x3C410u: return start_addr_5_;
            case 0x3C414u: return start_addr_6_;
            case 0x3C418u: return start_addr_7_;
            case 0x3C41Cu: return start_addr_8_;
            case 0x3C402u:
            case 0x3C406u:
            case 0x3C40Au:
            case 0x3C40Eu:
            case 0x3C412u:
            case 0x3C416u:
            case 0x3C41Au:
            case 0x3C41Eu: return 0x0000u;  /* high half of address word */
            case 0x3C220u: return buffer_count_;
            case 0x3C222u: return 0x0000u;
            case 0x3C800u: return start_buffer_;
            case 0x3C802u: return 0x0000u;
            case 0x3C880u: return last_cmd_;
            case 0x3C882u: return 0x0000u;
            case 0x3C884u: return last_cmd_;  /* direct-command shadow used by TFFS status probe */
            case 0x3C886u: return 0x0000u;
            case 0x3C8C0u: return sys_cfg_;
            case 0x3C8C2u: return 0x0000u;
            case 0x3C900u: return ctrl_status_;
            case 0x3C902u: return 0x0000u;
            case 0x3C904u: return CompletionLow();
            case 0x3C906u: return CompletionHigh();
            default:
                HaltUnsupportedAccess("OneNAND unknown register read", MmioBase() + off, 0);
        }
    }
    void RegWrite16(uint32_t off, uint16_t v) {
        if (IsTffsStatusOrEccReg(off)) {
            /* W1C/clear-style status and ECC-result registers. Values are
               intentionally not latched: the model reports no ECC/controller
               error on subsequent reads. */
            (void)v;
            return;
        }
        switch (off) {
            case 0x3C400u: start_addr_1_ = v; break;
            case 0x3C404u: start_addr_2_ = v; break;
            case 0x3C408u: start_addr_3_ = v; break;
            case 0x3C40Cu: start_addr_4_ = v; break;
            case 0x3C410u: start_addr_5_ = v; break;
            case 0x3C414u: start_addr_6_ = v; break;
            case 0x3C418u: start_addr_7_ = v; break;
            case 0x3C41Cu: start_addr_8_ = v; break;
            case 0x3C800u: start_buffer_ = v; break;
            case 0x3C880u: RunCommand(v); break;
            case 0x3C884u:
            case 0x3C886u: RunCommand(v); break;  /* TFFS writes 0x44704470 here during status/direct command phase */
            case 0x3C8C0u: sys_cfg_ = v; break;
            case 0x3C900u: ctrl_status_ = 0; break;   /* W1C-like clear */
            case 0x3C902u: break;
            case 0x3C904u: /* writing CSR clears it */ break;
            case 0x3C906u: break;
            case 0x3C220u: buffer_count_ = v; break;
            case 0x3C002u:
            case 0x3C006u:
            case 0x3C00Au:
            case 0x3C00Eu:
            case 0x3C012u:
            case 0x3C01Au:
            case 0x3C222u:
            case 0x3C402u:
            case 0x3C406u:
            case 0x3C40Au:
            case 0x3C40Eu:
            case 0x3C412u:
            case 0x3C416u:
            case 0x3C41Au:
            case 0x3C41Eu:
            case 0x3C802u:
            case 0x3C882u:
            case 0x3C8C2u: break;  /* high half of a supported 32-bit host access */
            default:
                HaltUnsupportedAccess("OneNAND unknown register write", MmioBase() + off, v);
        }
    }

    std::vector<uint8_t>             backing_;
    std::vector<uint8_t>             spare_;
    std::array<uint8_t, kBufferRamEnd> data_ram_{};
    std::array<uint8_t, kPageSpare>  spare_ram_{};
    std::array<uint8_t, kBootStateMirrorSize> boot_state_mirror_{};
    uint16_t start_addr_1_ = 0;
    uint16_t start_addr_2_ = 0;
    uint16_t start_addr_3_ = 0;
    uint16_t start_addr_4_ = 0;
    uint16_t start_addr_5_ = 0;
    uint16_t start_addr_6_ = 0;
    uint16_t start_addr_7_ = 0;
    uint16_t start_addr_8_ = 0;
    uint16_t start_buffer_ = 0x0800;
    uint16_t sys_cfg_      = 0;
    uint16_t ctrl_status_  = 0;
    uint16_t buffer_count_ = 0;
    uint8_t  last_cmd_     = 0;
};

}  /* namespace */

REGISTER_SERVICE(SiemensMp377OneNand);

