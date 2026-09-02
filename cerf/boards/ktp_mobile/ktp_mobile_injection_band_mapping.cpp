#include "ktp_mobile_injection_band_mapping.h"

#include "../../boards/board_context.h"
#include "../../boot/cerf_injection_region.h"
#include "../../core/cerf_emulator.h"
#include "../../core/device_config.h"
#include "../../core/log.h"
#include "../../socs/guest_cpu_reset.h"
#include "../../cpu/emulated_memory.h"
#include "../../jit/arm/arm_mmu.h"

REGISTER_SERVICE(KtpMobileInjectionBandMapping);

bool KtpMobileInjectionBandMapping::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && BoardContext::IsKtpMobile(bd->GetBoard()) && emu_.Get<DeviceConfig>().guest_additions;
}

/* The guest rebuilds its translation tables after a CPU reset, so the entries
   this service wrote are gone and must be installed again. */
void KtpMobileInjectionBandMapping::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { installed_ = false; });
}

void KtpMobileInjectionBandMapping::EnsureBandMappedIntoGuestTables() {
    if (installed_) return;

    const ArmMmuState* st = emu_.Get<ArmMmu>().State();
    if (!st || !st->effective_control_register.bits.m) return;

    auto& region = emu_.Get<CerfInjectionRegion>();
    const uint32_t va_base = region.BandVaBase();
    const uint32_t pa_base = region.BandPaBase();
    const uint32_t size = region.BandSize();
    if (!va_base || !size) return;

    /* ARM DDI 0406C.c B3.5.1 (p. B3-1323): TTBCR.N selects TTBR1 for the top
       of the VA space; the L1 index is VA[31:20]. */
    const uint32_t ttbcr_n = st->ttbcr & 7u;
    const bool use_ttbr1 = ttbcr_n != 0u && (va_base >> (32u - ttbcr_n)) != 0u;
    const uint32_t l1_base =
        use_ttbr1 ? (st->ttbr1 & 0xFFFFC000u) : (st->translation_table_base.word & ~((1u << (14u - ttbcr_n)) - 1u));

    auto& mem = emu_.Get<EmulatedMemory>();
    uint8_t* l1h = mem.TryTranslateWrite(l1_base | ((va_base >> 20) << 2));
    if (!l1h) return;
    const uint32_t l1 = *reinterpret_cast<uint32_t*>(l1h);

    /* ARM DDI 0406C.c Figure B3-4: L1 type 0b01 is a coarse page table whose
       base is bits[31:10].  The CE8 OAL leaves this table in place over the
       static window, so the entries can be filled without disturbing it. */
    if ((l1 & 3u) != 1u) return;
    const uint32_t l2_base = l1 & 0xFFFFFC00u;

    /* ARM DDI 0406C.d Figure B3-5: a small-page descriptor is PA[31:12] with
       the attribute bits in [11:0].  B3.5.2 (p. B3-1324) on the 0b1x encoding:
       "In this descriptor format, bit[0] of the descriptor is the XN bit."
       The band holds the injected stub's code, so XN is cleared.

       The attributes are the ones CE itself uses when it commits a page of
       this static window.  They are read from a page the guest has already
       committed: the band's own coarse table is empty on some panels, so the
       search widens to the neighbouring 1 MB coarse tables of the same
       window until a valid small page is found. */
    /* ARM DDI 0406C.d Table B3-10: C and B select the memory type; a page
       with neither set is Strongly-ordered or Device, which is not what the
       stub's code should run from.  Only pages CE has committed as Normal
       memory are accepted as the attribute donor. */
    const auto attrs_from_l2 = [&](uint32_t table_base, uint32_t& out) {
        for (uint32_t i = 0; i < 256u; ++i) {
            uint8_t* e = mem.TryTranslateWrite(table_base | (i << 2));
            if (!e) continue;
            const uint32_t v = *reinterpret_cast<uint32_t*>(e);
            if ((v & 2u) == 0u) continue;
            if ((v & 0x0Cu) == 0u) continue; /* C=0 and B=0: not Normal */
            out = (v & 0xFFFu) & ~1u;
            return true;
        }
        return false;
    };

    uint32_t attrs = 0;
    bool have_attrs = attrs_from_l2(l2_base, attrs);
    /* The band sits just past the MMIO spans of the OAL table, so its
       neighbours are Device pages.  The donor is therefore looked for across
       every coarse table this L1 references, nearest first. */
    for (uint32_t step = 1u; !have_attrs && step < 0x1000u; ++step) {
        for (int dir = -1; dir <= 1 && !have_attrs; dir += 2) {
            const int64_t idx = static_cast<int64_t>(va_base >> 20) + dir * static_cast<int64_t>(step);
            if (idx < 0 || idx >= 0x1000) continue;
            uint8_t* nh = mem.TryTranslateWrite(l1_base | (static_cast<uint32_t>(idx) << 2));
            if (!nh) continue;
            const uint32_t n = *reinterpret_cast<uint32_t*>(nh);
            if ((n & 3u) != 1u) continue;
            have_attrs = attrs_from_l2(n & 0xFFFFFC00u, attrs);
        }
    }
    if (!have_attrs) return;

    /* Where the guest has committed the band's first page for its own use it
       is left untouched; where it has not, that page belongs to the band too. */
    uint8_t* first = mem.TryTranslateWrite(l2_base | (((va_base >> 12) & 0xFFu) << 2));
    const bool guest_owns_first = first && (*reinterpret_cast<uint32_t*>(first) & 2u) != 0u;

    uint32_t mapped = 0;
    for (uint32_t off = guest_owns_first ? 0x1000u : 0u; off < size; off += 0x1000u) {
        const uint32_t va = va_base + off;
        uint8_t* e = mem.TryTranslateWrite(l2_base | (((va >> 12) & 0xFFu) << 2));
        if (!e) continue;
        *reinterpret_cast<uint32_t*>(e) = (pa_base + off) | attrs;
        ++mapped;
    }

    installed_ = true;
    LOG(GuestAdditions,
        "KTP Mobile injection band mapped into the guest coarse table at PA "
        "0x%08X: VA 0x%08X -> PA 0x%08X, %u pages, attrs 0x%03X, "
        "guest owns first page: %s\n",
        l2_base, va_base + (guest_owns_first ? 0x1000u : 0u), pa_base + (guest_owns_first ? 0x1000u : 0u), mapped,
        attrs, guest_owns_first ? "yes" : "no");
}
