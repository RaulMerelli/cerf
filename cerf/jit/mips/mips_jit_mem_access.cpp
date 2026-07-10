#include "mips_jit.h"

#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"
#include "../../peripherals/peripheral_dispatcher.h"

/* MipsJit guest-memory-access helpers the JIT-emitted code CALLs: translate the
   effective address (kseg fold / software TLB) then load or store, including the
   unaligned LWL / SWL / SWR / SDL / SDR byte merges. */

uint32_t MipsJit::MmioRead(uint32_t va, uint32_t pa, uint32_t width, const char* who) {
    if (peripheral_->IsPeripheralAddress(pa)) {
        switch (width) {
            case 1:  return peripheral_->ReadByte(pa);
            case 2:  return peripheral_->ReadHalf(pa);
            default: return peripheral_->ReadWord(pa);
        }
    }
    LOG(Caution, "%s: unmapped MMIO read va=0x%08X pa=0x%08X pc=0x%08X (no peripheral registered)\n",
        who, va, pa, cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsJit::MmioWrite(uint32_t va, uint32_t pa, uint32_t value, uint32_t width, const char* who) {
    if (peripheral_->IsPeripheralAddress(pa)) {
        switch (width) {
            case 1:  peripheral_->WriteByte(pa, static_cast<uint8_t>(value));  return;
            case 2:  peripheral_->WriteHalf(pa, static_cast<uint16_t>(value)); return;
            default: peripheral_->WriteWord(pa, value);                        return;
        }
    }
    LOG(Caution, "%s: unmapped MMIO write va=0x%08X pa=0x%08X val=0x%08X pc=0x%08X (no peripheral "
        "registered)\n", who, va, pa, value, cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __fastcall MipsJit::StoreWordHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    if (va & 3u) {                        /* SW requires a 4-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kWrite);   /* AdES; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kWrite, r);  /* TLBS/Mod; SEH unwind */
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        jit->InvalidateOnRamWrite(host, 4u);
        std::memcpy(host, &value, sizeof(value));
        return;
    }
    jit->MmioWrite(va, pa, value, 4, "MipsJit::StoreWordHelper");
}

void __fastcall MipsJit::StoreHalfHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    if (va & 1u) {                        /* SH requires a 2-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kWrite);   /* AdES; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kWrite, r);  /* TLBS/Mod; SEH unwind */
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        jit->InvalidateOnRamWrite(host, 2u);
        const uint16_t h = static_cast<uint16_t>(value);
        std::memcpy(host, &h, sizeof(h));
        return;
    }
    jit->MmioWrite(va, pa, value, 2, "MipsJit::StoreHalfHelper");
}

void __fastcall MipsJit::StoreByteHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    StoreByteXlate(jit, va, static_cast<uint8_t>(value), "MipsJit::StoreByteHelper");
}

uint32_t __fastcall MipsJit::LoadWordHelper(uint32_t va, MipsJit* jit) {
    if (va & 3u) {                        /* LW requires a 4-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kRead);    /* AdEL; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kRead, r);  /* TLBL; SEH unwind */
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        uint32_t value = 0;
        std::memcpy(&value, host, sizeof(value));
        return value;
    }
    return jit->MmioRead(va, pa, 4, "MipsJit::LoadWordHelper");
}

uint32_t __fastcall MipsJit::LoadByteHelper(uint32_t va, MipsJit* jit) {
    uint32_t pa = 0;                      /* a byte EA is always aligned */
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kRead, r);  /* TLBL; SEH unwind */
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        return *host;
    }
    return jit->MmioRead(va, pa, 1, "MipsJit::LoadByteHelper");
}

uint32_t __fastcall MipsJit::LoadHalfHelper(uint32_t va, MipsJit* jit) {
    if (va & 1u) {                        /* LH/LHU require a 2-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kRead);    /* AdEL; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kRead, r);  /* TLBL; SEH unwind */
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        uint16_t value = 0;
        std::memcpy(&value, host, sizeof(value));
        return value;                     /* zero-extended into the uint32 return */
    }
    return jit->MmioRead(va, pa, 2, "MipsJit::LoadHalfHelper");
}

uint64_t __fastcall MipsJit::LoadDwordHelper(uint32_t va, MipsJit* jit) {
    if (va & 7u) {                        /* LD requires an 8-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kRead);    /* AdEL; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kRead, r);  /* TLBL; SEH unwind */
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        uint64_t value = 0;
        std::memcpy(&value, host, sizeof(value));
        return value;
    }
    if (jit->peripheral_->IsPeripheralAddress(pa)) {
        return jit->peripheral_->ReadDword(pa);
    }
    LOG(Caution, "MipsJit::LoadDwordHelper: unmapped MMIO read va=0x%08X pa=0x%08X pc=0x%08X "
            "(no peripheral registered)\n", va, pa, jit->cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsJit::StoreByteXlate(MipsJit* jit, uint32_t va, uint8_t value,
                            const char* who) {
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kWrite, r);  /* TLBS/Mod; SEH unwind */
    }
    uint8_t* host = jit->memory_->TryTranslateWrite(pa);
    if (!host) {
        jit->MmioWrite(va, pa, value, 1, who);
        return;
    }
    jit->InvalidateOnRamWrite(host, 1u);
    *host = value;
}

void __fastcall MipsJit::SdrHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* Little-endian SDR: store the low ((va&7)^7)+1 bytes of gpr[rt], byte n ->
       mem[va+n] = rt>>(n*8). (QEMU tcg/ldst_helper.c helper_sdr + get_lmask.) */
    const uint64_t val   = jit->cpu_state_.gpr[rt];
    const uint32_t count = ((va & 7u) ^ 7u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate(jit, va + n, static_cast<uint8_t>(val >> (n * 8u)),
                       "MipsJit::SdrHelper");
    }
}

void __fastcall MipsJit::SdlHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* Little-endian SDL: store the high (va&7)+1 bytes of gpr[rt], byte n ->
       mem[va-n] = rt>>((7-n)*8). (QEMU tcg/ldst_helper.c helper_sdl + get_lmask.) */
    const uint64_t val   = jit->cpu_state_.gpr[rt];
    const uint32_t count = (va & 7u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate(jit, va - n, static_cast<uint8_t>(val >> ((7u - n) * 8u)),
                       "MipsJit::SdlHelper");
    }
}

void __fastcall MipsJit::LwlHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* LE: load the aligned word, shift it into the high bytes by ((va&3)^3)*8,
       OR in the kept low bytes of rt, sign-extend the 32-bit result. (QEMU
       translate.c gen_lxl + ext32s, OPC_LWL.) */
    const uint32_t shift  = ((va & 3u) ^ 3u) * 8u;
    const uint32_t w      = LoadWordHelper(va & ~3u, jit);  /* aligned: translate+load+fault */
    const uint32_t old    = static_cast<uint32_t>(jit->cpu_state_.gpr[rt]);
    const uint32_t mask   = 0xFFFFFFFFu << shift;
    const uint32_t merged = (w << shift) | (old & ~mask);
    if (rt != 0) {
        jit->cpu_state_.gpr[rt] =
            static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(merged)));
    }
}

void __fastcall MipsJit::LwrHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* LE: load the aligned word, shift it right by s=(va&3)*8 so the right
       bytes land in rt's low end, keep rt's high bytes via (~1)<<(s^31), OR,
       sign-extend the 32-bit result. (QEMU translate.c gen_lxr + ext32s,
       OPC_LWR, MO_UL.) */
    const uint32_t s         = (va & 3u) * 8u;
    const uint32_t w         = LoadWordHelper(va & ~3u, jit);  /* aligned: translate+load+fault */
    const uint32_t loaded    = w >> s;
    const uint32_t keep_mask = 0xFFFFFFFEu << (s ^ 31u);       /* (~1) << (s^31) */
    const uint32_t old       = static_cast<uint32_t>(jit->cpu_state_.gpr[rt]);
    const uint32_t merged    = loaded | (old & keep_mask);
    if (rt != 0) {
        jit->cpu_state_.gpr[rt] =
            static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(merged)));
    }
}

void __fastcall MipsJit::LdlHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* LE: load the aligned doubleword, shift it left by shift=((va&7)^7)*8 so the
       left bytes land in rt's high end, keep rt's low bytes via ~((~0)<<shift), OR.
       Full 64-bit (no sext). (QEMU translate.c gen_lxl, OPC_LDL, MO_UQ.) */
    const uint32_t shift  = ((va & 7u) ^ 7u) * 8u;
    const uint64_t w      = LoadDwordHelper(va & ~7u, jit);  /* aligned: translate+load+fault */
    const uint64_t old    = jit->cpu_state_.gpr[rt];
    const uint64_t mask   = (~0ull) << shift;
    const uint64_t merged = (w << shift) | (old & ~mask);
    if (rt != 0) {
        jit->cpu_state_.gpr[rt] = merged;
    }
}

void __fastcall MipsJit::LdrHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* LE: load the aligned dword, shift it right by s=(va&7)*8 so the right bytes
       land in rt's low end, keep rt's high bytes via (~1)<<(s^63), OR. Full 64-bit
       (no sext). (QEMU translate.c gen_lxr, OPC_LDR, MO_UQ.) */
    const uint32_t s         = (va & 7u) * 8u;
    const uint64_t w         = LoadDwordHelper(va & ~7u, jit);  /* aligned: translate+load+fault */
    const uint64_t loaded    = w >> s;
    const uint64_t keep_mask = (~1ull) << (s ^ 63u);
    const uint64_t old       = jit->cpu_state_.gpr[rt];
    const uint64_t merged    = loaded | (old & keep_mask);
    if (rt != 0) {
        jit->cpu_state_.gpr[rt] = merged;
    }
}

void __fastcall MipsJit::SwrHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* Little-endian SWR: store the low ((va&3)^3)+1 bytes of gpr[rt][31:0],
       byte n -> mem[va+n] = rt>>(n*8). (QEMU tcg/ldst_helper.c helper_swr.) */
    const uint32_t val   = static_cast<uint32_t>(jit->cpu_state_.gpr[rt]);
    const uint32_t count = ((va & 3u) ^ 3u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate(jit, va + n, static_cast<uint8_t>(val >> (n * 8u)),
                       "MipsJit::SwrHelper");
    }
}

void __fastcall MipsJit::SwlHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    /* Little-endian SWL: store the high (va&3)+1 bytes of gpr[rt][31:0], byte n
       -> mem[va-n] = rt>>((3-n)*8). (QEMU tcg/ldst_helper.c helper_swl.) */
    const uint32_t val   = static_cast<uint32_t>(jit->cpu_state_.gpr[rt]);
    const uint32_t count = (va & 3u) + 1u;
    for (uint32_t n = 0; n < count; ++n) {
        StoreByteXlate(jit, va - n, static_cast<uint8_t>(val >> ((3u - n) * 8u)),
                       "MipsJit::SwlHelper");
    }
}

void __fastcall MipsJit::StoreDwordHelper(uint32_t va, uint32_t rt, MipsJit* jit) {
    if (va & 7u) {                        /* SD requires an 8-byte-aligned EA */
        jit->RaiseAddressError(va, MipsAccess::kWrite);   /* AdES; SEH unwind */
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_->Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        jit->RaiseTlbException(va, MipsAccess::kWrite, r);  /* TLBS/Mod; SEH unwind */
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        jit->InvalidateOnRamWrite(host, 8u);
        std::memcpy(host, &jit->cpu_state_.gpr[rt], sizeof(uint64_t));
        return;
    }
    if (jit->peripheral_->IsPeripheralAddress(pa)) {
        jit->peripheral_->WriteDword(pa, jit->cpu_state_.gpr[rt]);
        return;
    }
    LOG(Caution, "MipsJit::StoreDwordHelper: unmapped MMIO write va=0x%08X pa=0x%08X pc=0x%08X "
            "(no peripheral registered)\n", va, pa, jit->cpu_state_.pc);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
