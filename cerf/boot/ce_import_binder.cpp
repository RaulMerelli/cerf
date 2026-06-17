#include "ce_import_binder.h"

#include "pe_image.h"
#include "rom_parser_service.h"

#include "../core/cerf_emulator.h"
#include "../core/log.h"
#include "../cpu/emulated_memory.h"
#include "../boards/page_table_builder.h"

#include <cstring>
#include <string>

REGISTER_SERVICE(CeImportBinder);

namespace {

/* PE/COFF on-disk layout (Microsoft PE/COFF spec). */
constexpr uint32_t kOrdinalFlag32  = 0x80000000u;   /* IMAGE_ORDINAL_FLAG32 */
constexpr uint32_t kImportDescSize = 20u;           /* IMAGE_IMPORT_DESCRIPTOR */
constexpr uint32_t kImpOffOft      = 0u;            /* OriginalFirstThunk */
constexpr uint32_t kImpOffName     = 12u;           /* Name (rva)         */
constexpr uint32_t kImpOffFt       = 16u;           /* FirstThunk (IAT)   */

/* IMAGE_EXPORT_DIRECTORY field offsets. */
constexpr uint32_t kExpOffBase      = 16u;          /* Base (ordinal base)   */
constexpr uint32_t kExpOffNumFuncs  = 20u;          /* NumberOfFunctions     */
constexpr uint32_t kExpOffNumNames  = 24u;          /* NumberOfNames         */
constexpr uint32_t kExpOffAddrFuncs = 28u;          /* AddressOfFunctions    */
constexpr uint32_t kExpOffAddrNames = 32u;          /* AddressOfNames        */
constexpr uint32_t kExpOffAddrOrds  = 36u;          /* AddressOfNameOrdinals */

constexpr uint32_t kBadOff = 0xFFFFFFFFu;

uint32_t RvaToFileOff(const PeImage& pe, uint32_t rva) {
    for (const auto& s : pe.Sections()) {
        const uint32_t span = (s.psize > s.vsize) ? s.psize : s.vsize;
        if (rva >= s.rva && rva < s.rva + span) return s.pe_file_off + (rva - s.rva);
    }
    return kBadOff;
}

uint32_t PeRead32(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8)
         | (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24);
}

void PeWrite32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off + 0] = uint8_t(v);
    b[off + 1] = uint8_t(v >> 8);
    b[off + 2] = uint8_t(v >> 16);
    b[off + 3] = uint8_t(v >> 24);
}

std::string PeReadAscii(const std::vector<uint8_t>& b, uint32_t off) {
    std::string s;
    for (uint32_t i = off; i < b.size() && b[i]; ++i) s.push_back(char(b[i]));
    return s;
}

const ParsedTOCentry* FindRomModule(const ParsedRom& rom, const char* name) {
    for (const auto& xip : rom.xips)
        for (const auto& m : xip.toc.modules)
            if (_stricmp(m.lpszFileName.c_str(), name) == 0) return &m;
    return nullptr;
}

}  /* namespace */

void CeImportBinder::BindImports(std::vector<uint8_t>& bytes, const PeImage& pe,
                                 const E32RomLayout& layout) {
    const uint32_t imp_rva  = pe.DirRva(1);   /* IMAGE_DIRECTORY_ENTRY_IMPORT */
    const uint32_t imp_size = pe.DirSize(1);
    if (imp_rva == 0 || imp_size == 0) return;

    /* Descriptors / OFT / names are RVAs the loader never relocates, so read them
       from the pristine PE; only the IAT (FirstThunk) is patched into `bytes`. */
    const std::vector<uint8_t>& src = pe.Bytes();
    uint32_t bound = 0;

    for (uint32_t d = imp_rva; ; d += kImportDescSize) {
        const uint32_t doff = RvaToFileOff(pe, d);
        if (doff == kBadOff || size_t(doff) + kImportDescSize > src.size()) {
            LOG(Caution, "CeImportBinder: import descriptor rva 0x%X out of image\n", d);
            CerfFatalExit();
        }
        const uint32_t oft_rva  = PeRead32(src, doff + kImpOffOft);
        const uint32_t name_rva = PeRead32(src, doff + kImpOffName);
        const uint32_t ft_rva   = PeRead32(src, doff + kImpOffFt);
        if (name_rva == 0 && ft_rva == 0) break;   /* null-terminator descriptor */

        const uint32_t noff = RvaToFileOff(pe, name_rva);
        if (noff == kBadOff) {
            LOG(Caution, "CeImportBinder: import DLL-name rva 0x%X out of image\n", name_rva);
            CerfFatalExit();
        }
        const std::string dll = PeReadAscii(src, noff);
        const GuestExportDir ed = LocateModuleExports(dll.c_str(), layout);

        const uint32_t thunk_rva = oft_rva ? oft_rva : ft_rva;
        for (uint32_t i = 0; ; ++i) {
            const uint32_t toff = RvaToFileOff(pe, thunk_rva + i * 4u);
            const uint32_t foff = RvaToFileOff(pe, ft_rva + i * 4u);
            if (toff == kBadOff || size_t(toff) + 4 > src.size()
                || foff == kBadOff || size_t(foff) + 4 > bytes.size()) {
                LOG(Caution, "CeImportBinder: thunk rva out of image (%s)\n", dll.c_str());
                CerfFatalExit();
            }
            const uint32_t orig = PeRead32(src, toff);
            if (orig == 0) break;   /* end of this DLL's thunk list */

            uint32_t resolved;
            if (orig & kOrdinalFlag32) {
                resolved = ResolveOrdinal(ed, orig & 0xFFFFu, dll.c_str());
            } else {
                const uint32_t inoff = RvaToFileOff(pe, orig + 2u);  /* skip Hint u16 */
                if (inoff == kBadOff) {
                    LOG(Caution, "CeImportBinder: import-by-name rva 0x%X out of image\n", orig);
                    CerfFatalExit();
                }
                resolved = ResolveName(ed, PeReadAscii(src, inoff).c_str(), dll.c_str());
            }
            PeWrite32(bytes, foff, resolved);
            ++bound;
        }
    }
    LOG(GuestAdditions, "CeImportBinder: pre-bound %u import(s)\n", bound);
}

CeImportBinder::GuestExportDir
CeImportBinder::LocateModuleExports(const char* dll, const E32RomLayout& layout) {
    const ParsedTOCentry* m =
        FindRomModule(emu_.Get<RomParserService>().Primary(), dll);
    if (!m) {
        LOG(Caution, "CeImportBinder: imported DLL '%s' not in ROM TOC\n", dll);
        CerfFatalExit();
    }
    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t e32_pa = pt.VaToPa(m->ulE32Offset);

    GuestExportDir ed;
    ed.vbase    = mem.ReadWord(e32_pa + layout.off_vbase);
    ed.exp_rva  = mem.ReadWord(e32_pa + layout.off_unit + 0u);   /* e32_unit[EXP].rva  */
    ed.exp_size = mem.ReadWord(e32_pa + layout.off_unit + 4u);   /* e32_unit[EXP].size */
    ed.objcnt   = mem.ReadHalf(e32_pa + layout.off_objcnt);
    ed.o32_pa   = pt.VaToPa(m->ulO32Offset);
    if (ed.exp_rva == 0) {
        LOG(Caution, "CeImportBinder: '%s' has no export directory\n", dll);
        CerfFatalExit();
    }
    return ed;
}

uint32_t CeImportBinder::MapRvaToPa(const GuestExportDir& ed, uint32_t rva) {
    auto& pt  = emu_.Get<PageTableBuilder>();
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t i = 0; i < ed.objcnt; ++i) {
        const uint32_t o    = ed.o32_pa + i * kO32RomSize;
        const uint32_t srva = mem.ReadWord(o + kO32OffRva);
        const uint32_t vsz  = mem.ReadWord(o + kO32OffVsize);
        if (rva >= srva && rva < srva + vsz) {
            const uint32_t dataptr = mem.ReadWord(o + kO32OffDataptr);
            return pt.VaToPa(dataptr + (rva - srva));
        }
    }
    LOG(Caution, "CeImportBinder: rva 0x%X not in any section\n", rva);
    CerfFatalExit();
}

uint32_t CeImportBinder::ResolveOrdinal(const GuestExportDir& ed, uint32_t ordinal,
                                        const char* dll) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t exp_pa  = MapRvaToPa(ed, ed.exp_rva);
    const uint32_t base    = mem.ReadWord(exp_pa + kExpOffBase);
    const uint32_t nfuncs  = mem.ReadWord(exp_pa + kExpOffNumFuncs);
    const uint32_t af_rva  = mem.ReadWord(exp_pa + kExpOffAddrFuncs);
    if (ordinal < base || (ordinal - base) >= nfuncs) {
        LOG(Caution, "CeImportBinder: %s ordinal %u outside export range [%u,%u)\n",
            dll, ordinal, base, base + nfuncs);
        CerfFatalExit();
    }
    const uint32_t func_rva = mem.ReadWord(MapRvaToPa(ed, af_rva + (ordinal - base) * 4u));
    if (func_rva == 0) {
        LOG(Caution, "CeImportBinder: %s ordinal %u not present\n", dll, ordinal);
        CerfFatalExit();
    }
    if (func_rva >= ed.exp_rva && func_rva < ed.exp_rva + ed.exp_size) {
        LOG(Caution, "CeImportBinder: %s ordinal %u is a forwarder (unsupported)\n",
            dll, ordinal);
        CerfFatalExit();
    }
    return ed.vbase + func_rva;
}

uint32_t CeImportBinder::ResolveName(const GuestExportDir& ed, const char* fn,
                                     const char* dll) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t exp_pa    = MapRvaToPa(ed, ed.exp_rva);
    const uint32_t nnames    = mem.ReadWord(exp_pa + kExpOffNumNames);
    const uint32_t names_rva = mem.ReadWord(exp_pa + kExpOffAddrNames);
    const uint32_t ords_rva  = mem.ReadWord(exp_pa + kExpOffAddrOrds);
    const uint32_t af_rva    = mem.ReadWord(exp_pa + kExpOffAddrFuncs);
    for (uint32_t i = 0; i < nnames; ++i) {
        const uint32_t name_rva = mem.ReadWord(MapRvaToPa(ed, names_rva + i * 4u));
        if (!GuestAsciiEquals(ed, name_rva, fn)) continue;
        const uint16_t fidx = mem.ReadHalf(MapRvaToPa(ed, ords_rva + i * 2u));
        const uint32_t func_rva = mem.ReadWord(MapRvaToPa(ed, af_rva + uint32_t(fidx) * 4u));
        if (func_rva >= ed.exp_rva && func_rva < ed.exp_rva + ed.exp_size) {
            LOG(Caution, "CeImportBinder: %s!%s is a forwarder (unsupported)\n", dll, fn);
            CerfFatalExit();
        }
        return ed.vbase + func_rva;
    }
    LOG(Caution, "CeImportBinder: %s does not export '%s'\n", dll, fn);
    CerfFatalExit();
}

bool CeImportBinder::GuestAsciiEquals(const GuestExportDir& ed, uint32_t name_rva,
                                      const char* s) {
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t i = 0; ; ++i) {
        const uint8_t g = mem.ReadByte(MapRvaToPa(ed, name_rva + i));
        if (g != uint8_t(s[i])) return false;
        if (g == 0) return true;
    }
}
