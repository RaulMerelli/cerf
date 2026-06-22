#include "mips_jit.h"

#include <cstring>

#include "../../core/log.h"
#include "../../cpu/emulated_memory.h"

/* MipsJit guest-memory-access helpers the JIT-emitted code CALLs: translate the
   effective address (kseg fold / software TLB) then load or store, including the
   unaligned LWL / SWL / SWR / SDL / SDR byte merges. */

void __fastcall MipsJit::StoreWordHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    if (va & 3u) {                        /* SW requires a 4-byte-aligned EA */
        LOG(Caution, "MipsJit::StoreWordHelper: misaligned SW va=0x%08X "
                "(AdES exception delivery not yet implemented)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::StoreWordHelper: TLB result %d on write va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        std::memcpy(host, &value, sizeof(value));
        return;
    }
    LOG(Caution, "MipsJit::StoreWordHelper: MMIO write va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __fastcall MipsJit::StoreHalfHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    if (va & 1u) {                        /* SH requires a 2-byte-aligned EA */
        LOG(Caution, "MipsJit::StoreHalfHelper: misaligned SH va=0x%08X "
                "(AdES exception delivery not yet implemented)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::StoreHalfHelper: TLB result %d on write va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        const uint16_t h = static_cast<uint16_t>(value);
        std::memcpy(host, &h, sizeof(h));
        return;
    }
    LOG(Caution, "MipsJit::StoreHalfHelper: MMIO write va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void __fastcall MipsJit::StoreByteHelper(uint32_t va, uint32_t value, MipsJit* jit) {
    StoreByteXlate(jit, va, static_cast<uint8_t>(value), "MipsJit::StoreByteHelper");
}

uint32_t __fastcall MipsJit::LoadWordHelper(uint32_t va, MipsJit* jit) {
    if (va & 3u) {                        /* LW requires a 4-byte-aligned EA */
        LOG(Caution, "MipsJit::LoadWordHelper: misaligned LW va=0x%08X "
                "(AdEL exception delivery not yet implemented)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::LoadWordHelper: TLB result %d on read va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        uint32_t value = 0;
        std::memcpy(&value, host, sizeof(value));
        return value;
    }
    LOG(Caution, "MipsJit::LoadWordHelper: MMIO read va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint32_t __fastcall MipsJit::LoadByteHelper(uint32_t va, MipsJit* jit) {
    uint32_t pa = 0;                      /* a byte EA is always aligned */
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::LoadByteHelper: TLB result %d on read va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        return *host;
    }
    LOG(Caution, "MipsJit::LoadByteHelper: MMIO read va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

uint64_t __fastcall MipsJit::LoadDwordHelper(uint32_t va, MipsJit* jit) {
    if (va & 7u) {                        /* LD requires an 8-byte-aligned EA */
        LOG(Caution, "MipsJit::LoadDwordHelper: misaligned LD va=0x%08X "
                "(AdEL exception delivery not yet implemented)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kRead, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::LoadDwordHelper: TLB result %d on read va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (const uint8_t* host = jit->memory_->TryTranslate(pa)) {
        uint64_t value = 0;
        std::memcpy(&value, host, sizeof(value));
        return value;
    }
    LOG(Caution, "MipsJit::LoadDwordHelper: MMIO read va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

void MipsJit::StoreByteXlate(MipsJit* jit, uint32_t va, uint8_t value,
                            const char* who) {
    uint32_t pa = 0;
    if (jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa) !=
        MipsTlbResult::kMatch) {
        LOG(Caution, "%s: TLB fault on byte va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n", who, va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint8_t* host = jit->memory_->TryTranslateWrite(pa);
    if (!host) {
        LOG(Caution, "%s: unwritable/MMIO byte va=0x%08X pa=0x%08X "
                "(MMIO dispatch not yet implemented)\n", who, va, pa);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
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
        LOG(Caution, "MipsJit::StoreDwordHelper: misaligned SD va=0x%08X "
                "(AdES exception delivery not yet implemented)\n", va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    uint32_t pa = 0;
    const MipsTlbResult r =
        jit->mmu_.Translate(&jit->cpu_state_, va, MipsAccess::kWrite, &pa);
    if (r != MipsTlbResult::kMatch) {
        LOG(Caution, "MipsJit::StoreDwordHelper: TLB result %d on write va=0x%08X "
                "(CP0 exception delivery not yet implemented)\n",
                static_cast<int>(r), va);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (uint8_t* host = jit->memory_->TryTranslateWrite(pa)) {
        std::memcpy(host, &jit->cpu_state_.gpr[rt], sizeof(uint64_t));
        return;
    }
    LOG(Caution, "MipsJit::StoreDwordHelper: MMIO write va=0x%08X pa=0x%08X "
            "(peripheral MMIO dispatch not yet implemented)\n", va, pa);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}
