#include "../trace_manager.h"

#include "../../boards/board_detector.h"
#include "../../core/cerf_emulator.h"
#include "../../core/log.h"
#include "../../jit/arm_mmu.h"
#include "../kernel_debug_sink.h"

#include <cstdio>
#include <string>

#if CERF_DEV_MODE

namespace {

/* Board-gated (the `.sec` boot has bundle CRC 0). Pins the SBOOT NK-launch:
   the DPS record-id walk (Bootloader.bin 0x8FF0BC84, cmp record.id,#6), the
   matched record's StartBlock handed to the loader (0x8FF0BEB4 LoadModule), the
   load result (0x8FF0BEB8), and the loader's fail-return (0x8FF0C48C). */
class Sync2DpsOffsetTrace : public Service {
public:
    using Service::Service;

    void OnReady() override {
        auto* bd = emu_.TryGet<BoardDetector>();
        if (!bd || bd->GetSoc() != SocFamily::iMX51) return;
        LOG(Jit, "[NKLD] trace registered (iMX51)\n");
        auto& tm = emu_.Get<TraceManager>();

        tm.OnPc(0x8FF0BBF8u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] IPL Boot Image r4=%u\n", c.regs[4]);
        });
        tm.OnPc(0x8FF0BC84u, [this](const TraceContext& c) {
            if (walk_++ >= 8) return;
            LOG(Jit, "[NKLD] walk: record.id=0x%08X idx=%u (looking for ==6)\n",
                c.regs[3], c.regs[2]);
        });
        tm.OnPc(0x8FF0BEB4u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] LoadModule(ctx=0x%08X StartBlock=0x%08X &out=0x%08X)\n",
                c.regs[0], c.regs[1], c.regs[2]);
        });
        tm.OnPc(0x8FF0BEB8u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] LoadModule -> r0=0x%08X (%s)\n",
                c.regs[0], c.regs[0] ? "OK" : "FAIL");
        });
        tm.OnPc(0x8FF0C48Cu, [this](const TraceContext& c) {
            if (fail_++ >= 8) return;
            LOG(Jit, "[NKLD] loader fail-return @0x8FF0C48C lr=0x%08X "
                "r4(block)=0x%08X r5=0x%08X r6=0x%08X\n",
                c.regs[14], c.regs[4], c.regs[5], c.regs[6]);
        });
        /* Inside the ECEC scanner 0x8FF0BFCC: bad-block status, read result, sig. */
        tm.OnPc(0x8FF0C008u, [this](const TraceContext& c) {
            if (bfcc_++ >= 16) return;
            LOG(Jit, "[NKLD] BFCC badblk blk=r5=0x%08X badstat=r0=0x%08X\n",
                c.regs[5], c.regs[0]);
        });
        tm.OnPc(0x8FF0C040u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] BFCC read blk=r5=0x%08X result=r0=0x%08X\n",
                c.regs[5], c.regs[0]);
        });
        tm.OnPc(0x8FF0C050u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] BFCC ECEC-check [blk+0x40]=r3=0x%08X want=0x43454345\n",
                c.regs[3]);
        });
        /* Reserved-block registration (0x8FF09A10, r1=block, lr=caller): which
           blocks SBOOT marks reserved (the ECEC scanner skips these). */
        tm.OnPc(0x8FF09A10u, [this](const TraceContext& c) {
            if (resv_++ >= 48) return;
            LOG(Jit, "[NKLD] reserve-block blk=r1=0x%08X lr=0x%08X\n",
                c.regs[1], c.regs[14]);
        });
        /* Image-6 NK loader (0x8FF0C1E8): entry StartBlock + the two sig-memcmp
           results, to confirm the OS-region serving lands "BADT"/"MSFLSH60". */
        tm.OnPc(0x8FF0C1E8u, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] loader entry ctx=0x%08X StartBlock=%u\n",
                c.regs[0], c.regs[1]);
        });
        tm.OnPc(0x8FF0C27Cu, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] BADT memcmp blk=%u result=%u (0=match)\n",
                c.regs[4], c.regs[0]);
        });
        tm.OnPc(0x8FF0C2ECu, [](const TraceContext& c) {
            LOG(Jit, "[NKLD] MSFLSH60 memcmp blk=%u result=%u (0=match)\n",
                c.regs[4], c.regs[0]);
        });
        /* Block-status spare check (0x8FF0A494, r6=block, r0=read-result, [sp+8/9]
           = spare bytes 0/1): why block N+2 (NK ROMHDR) reads as bad. */
        tm.OnPc(0x8FF0A494u, [this](const TraceContext& c) {
            if (stat_++ >= 24) return;
            auto s8 = c.ReadVa8(c.regs[13] + 8);
            auto s9 = c.ReadVa8(c.regs[13] + 9);
            LOG(Jit, "[NKLD] status blk=r6=0x%08X readres=r0=0x%08X "
                "spare[8]=0x%02X spare[9]=0x%02X\n",
                c.regs[6], c.regs[0],
                s8 ? *s8 : 0xEEu, s9 ? *s9 : 0xEEu);
        });
        /* [EPITMAP] The OAL maps EPIT1 (phys 0x73FAC000) to a VA and stores it at
           global 0x80DB74F8; the OAL timer loops read [VA+0x10]=EPITCNR. A NULL
           global (map skipped/failed) -> kernel Data Abort at VA 0x10. These two
           .sec-build addresses bracket the two failure points. */
        tm.OnPc(0x801054C8u, [](const TraceContext& c) {  /* after cmn r6,#1 */
            LOG(Jit, "[EPITMAP] SYSINTR r6=0x%08X (==0xFFFFFFFF -> map skipped)\n",
                c.regs[6]);
        });
        tm.OnPc(0x801054D8u, [](const TraceContext& c) {  /* after phys2va, r0=VA */
            LOG(Jit, "[EPITMAP] phys2va(0x73FAC000) -> r0=0x%08X (==0 -> map failed)\n",
                c.regs[0]);
        });
        /* Timer-init fn 0x80105380 gates the EPIT map on clock-derived values; it
           early-exits to 0x80105568 (never reaching the map) unless the gates pass. */
        tm.OnPc(0x80105380u, [](const TraceContext& c) {  /* entry */
            LOG(Jit, "[EPITMAP] timerinit entry r0=0x%08X r1=0x%08X r2=0x%08X lr=0x%08X\n",
                c.regs[0], c.regs[1], c.regs[2], c.regs[14]);
        });
        tm.OnPc(0x801053C4u, [](const TraceContext& c) {  /* cmp r4,#0x8000 */
            LOG(Jit, "[EPITMAP] gate: r4(0x80105dbc)=0x%08X r5=0x%08X r7=0x%08X\n",
                c.regs[4], c.regs[5], c.regs[7]);
        });
        tm.OnPc(0x80105414u, [](const TraceContext&) {  /* gates passed -> map path */
            LOG(Jit, "[EPITMAP] gates PASSED -> proceeding to EPIT map\n");
        });
        tm.OnPc(0x80105568u, [](const TraceContext& c) {  /* early-exit join */
            LOG(Jit, "[EPITMAP] EARLY-EXIT @0x80105568 (map skipped) lr=0x%08X\n",
                c.regs[14]);
        });
        /* Sanity: 0x801051BC is the reader fn whose 0x801051E0 LDR faults — it
           provably executes. If THIS doesn't fire, nk.exe-VA OnPc hooks are not
           firing at all (vs the timer-init genuinely never running). */
        tm.OnPc(0x801051BCu, [](const TraceContext&) {
            LOG(Jit, "[EPITMAP] reader-fn 0x801051BC ENTERED r7-glob check next\n");
        });
        tm.OnPc(0x801060DCu, [](const TraceContext& c) {  /* timer-init caller */
            LOG(Jit, "[EPITMAP] timerinit CALLER 0x801060DC r0=0x%08X\n", c.regs[0]);
        });
        /* sub_80105DBC returns [phys2va(0x90000000)+0x70] = the EPIT clock-source
           value; it is 0 in CERF -> 1000/0 -> a2=0 -> timer-init early-exits. At
           0x80105DCC, r0 = phys2va(0x90000000) (the VA), before the +0x70 load. */
        tm.OnPc(0x80105DCCu, [this](const TraceContext& c) {
            if (bsp_dumped_) return;
            bsp_dumped_ = true;
            const uint32_t base = c.regs[0];  /* BSP_ARGS VA (phys2va 0x90000000) */
            for (uint32_t off = 0; off < 0x90; off += 0x10) {
                auto r = [&](uint32_t o){ auto v=c.ReadVa32(base+off+o); return v?*v:0xDEADBEEFu; };
                LOG(Jit, "[EPITMAP] BSP_ARGS[+0x%02X]: %08X %08X %08X %08X\n",
                    off, r(0), r(4), r(8), r(12));
            }
        });
        /* SBOOT clock computation: BSP_ARGS[+0x70] = sub_8ff11c9c(sl*r3, ...). It is
           0 in CERF -> the EPIT clock the OAL reads is 0. Capture the CCM-derived
           inputs (sl=r10, r3) and the divide args/result to pin which value is 0. */
        tm.OnPc(0x8FF06B74u, [](const TraceContext& c) {  /* mul r0,sl,r3 */
            LOG(Jit, "[EPITMAP] SBOOT clk-calc inputs: sl(r10)=0x%08X r3=0x%08X r5=0x%08X\n",
                c.regs[10], c.regs[3], c.regs[5]);
        });
        tm.OnPc(0x8FF06B80u, [](const TraceContext& c) {  /* bl div: r0=dividend r1=divisor */
            LOG(Jit, "[EPITMAP] SBOOT clk-calc div: r0(sl*r3)=0x%08X r1(divisor)=0x%08X\n",
                c.regs[0], c.regs[1]);
        });
        tm.OnPc(0x8FF06B8Cu, [](const TraceContext& c) {  /* str r0,[r6,#0x70] */
            LOG(Jit, "[EPITMAP] SBOOT clk-calc result -> BSP_ARGS[+0x70]=r0=0x%08X\n",
                c.regs[0]);
        });
        /* SBOOT reads a DPLL block (sl) DP_CTL at 0x8FF067B0 and gates the PLL freq
           on bit5 (UPEN): tst #0x20; beq -> freq 0. Capture sl (which DPLL) + the
           DP_CTL value to confirm UPEN is clear and which DPLL CERF resets to 0. */
        tm.OnPc(0x8FF067B0u, [this](const TraceContext& c) {  /* ldr r3,[sl] = DP_CTL */
            if (dpll_dumps_++ >= 6) return;
            const uint32_t sl = c.regs[10];
            auto dpctl = c.ReadVa32(sl);
            LOG(Jit, "[EPITMAP] SBOOT DPLL read: sl=0x%08X DP_CTL=0x%08X (bit5 UPEN=%u)\n",
                sl, dpctl ? *dpctl : 0xDEADBEEFu, dpctl ? ((*dpctl >> 5) & 1u) : 9u);
        });
        /* One instruction past the DP_CTL LDR: r3 now holds the value the guest
           actually loaded from sl (=0xBFF84000), and ArmMmu::LastDataPa() is that
           LDR's resolved PA. Answers whether SBOOT's uncached DPLL2 alias reaches
           CERF's DPLL2 (PA 0x83F84000) and what UPEN reads there. */
        tm.OnPc(0x8FF067B4u, [this](const TraceContext& c) {
            if (dpll_pa_dumps_++ >= 6) return;
            const uint32_t pa = c.emu.Get<ArmMmu>().LastDataPa();
            LOG(Jit, "[EPITMAP] SBOOT DPLL resolved: sl=0x%08X r3(DP_CTL)=0x%08X "
                "UPEN=%u resolvedPA=0x%08X\n",
                c.regs[10], c.regs[3], (c.regs[3] >> 5) & 1u, pa);
        });
        /* DPLL2 freq-calc inputs (the BSP_ARGS[+0x4c] PLL slot). At 0x8FF06808
           (umull) the four operands are loaded: r1=DP_OP[sl+8], r3=DP_MFN[sl+0x10],
           r0=DP_MFD[sl+0xc], r2=reference osc freq (fp/sb). All PLL slots came out 0;
           this pins whether the DPLL regs or the ref freq is the zero. */
        tm.OnPc(0x8FF06808u, [this](const TraceContext& c) {
            if (dpll_freq_dumps_++ >= 4) return;
            LOG(Jit, "[EPITMAP] DPLL2 freq inputs: sl=0x%08X DP_OP=0x%08X "
                "DP_MFD=0x%08X DP_MFN=0x%08X ref=0x%08X\n",
                c.regs[10], c.regs[1], c.regs[0], c.regs[3], c.regs[2]);
        });
        /* ARM high-vector IRQ entry (0xFFFF0018). Fires on every IRQ the JIT
           actually delivers (ArmCpuRaiseIrqException -> EnterException kIrq).
           r14 = banked LR_irq = interrupted PC + 4. Confirms whether the
           latched EPIT/TZIC source 40 is reaching the guest, and until when. */
        tm.OnPc(0xFFFF0018u, [this](const TraceContext& c) {
            if ((irqvec_++ & 0x3Fu) == 0)   /* every 64th delivery */
                LOG(Jit, "[IRQVEC] #%d IRQ delivered, interrupted PC(LR-4)=0x%08X\n",
                    irqvec_ - 1, c.regs[14] - 4u);
        });
        /* IPCMP RX ISR sub_C0E2213C: fires when the UART2 RX interrupt is
           serviced. If this never fires after InjectRx, the RX IRQ (TZIC src
           32) is not being delivered to the guest. */
        tm.OnPc(0xC0E2213Cu, [this](const TraceContext&) {
            if (rxisr_++ < 8) LOG(Jit, "[IPCRX] RX ISR fired (#%d)\n", rxisr_);
        });
        /* ipc.dll RX state machine sub_C093C7EC(a1=ctx, a2=pkt): fires when a
           deframed LINK packet reaches the link layer. a1+260 = link state. */
        tm.OnPc(0xC093C7ECu, [this](const TraceContext& c) {
            if (rxsm_++ >= 8) return;
            auto st = c.ReadVa32(c.regs[0] + 260u);
            LOG(Jit, "[IPCRX] ipc RX SM (#%d) ctx=0x%08X state=%d\n",
                rxsm_, c.regs[0], st ? static_cast<int>(*st) : -1);
        });
        /* ipc.dll TX SM sub_C0938888(a1, a2=link ctx in r1): reads the link
           state at [a2+84] each timer fire. case7 -> "### LINK UP"; the case6
           block expires the CPL retry to LINK DOWN (state 1). Shows whether the
           state my CPL set (->7) is ever seen, or it reverts to 1 first. */
        tm.OnPc(0xC0938888u, [this](const TraceContext& c) {
            if (txsm_++ >= 24) return;
            auto st = c.ReadVa32(c.regs[1] + 84u);
            LOG(Jit, "[TXSM] #%d ctx=0x%08X state=%d\n", txsm_, c.regs[1],
                st ? static_cast<int>(*st) : -1);
        });
        /* Head RX-ACK handler sub_C093BE18(ctx, ackpkt=r1): fires when the peer's
           injected ACK reaches the TX-release path. a2[0]=byte0 (Cid<<2|2),
           a2[1]=byte1 (ackseq<<1). Firing + no reboot = ACK accepted; the
           "INVALID ACK SEQ#" log inside the fn = ackseq out of the TX window. */
        tm.OnPc(0xC093BE18u, [this](const TraceContext& c) {
            if (rxack_++ >= 16) return;
            auto b0 = c.ReadVa8(c.regs[1] + 0u);
            auto b1 = c.ReadVa8(c.regs[1] + 1u);
            LOG(Jit, "[VMCUACK] head RX-ACK Cid=%u byte0=0x%02X byte1=0x%02X ackseq=%u\n",
                (b0 ? *b0 : 0u) >> 2, b0 ? *b0 : 0xEEu, b1 ? *b1 : 0xEEu,
                (b1 ? *b1 : 0u) >> 1);
        });
        /* ipc_ilprot SendRegisterSignalNotification sub_C08D9468(ctx=r0, sig=r1):
           the IL layer registers a signal with the VMCU and WaitForSingleObject's
           on a per-tid response. Repeated firing = the boot is driving IL
           registration that the peer never answers (the next blocker). */
        tm.OnPc(0xC08D9468u, [this](const TraceContext& c) {
            if (ilreg_++ < 24)
                LOG(Jit, "[ILREG] #%d RegisterSignalNotification ctx=0x%08X sig=0x%08X\n",
                    ilreg_, c.regs[0], c.regs[1]);
        });
        /* OAL OEMWriteDebugByte sub_80106360(char=r0): called per debug char even
           after the OEM sets the debug-disable flag (0x80DB7374) — it only skips
           the UART write when disabled. Capture R0 to recover the full NKDBG
           stream past the t+2 "Disabling Debug Serial Port". */
        tm.OnPc(0x80106360u, [this](const TraceContext& c) {
            c.emu.Get<KernelDebugSink>().EmitChar(
                static_cast<char>(c.regs[0] & 0xFFu), dbg_line_, "OAL");
        });
        /* IPCMP deframe finalize sub_C0E22780: a buffer completed + checksum
           checked. sub_C0E21528 = the indicate-up call (only reached when the
           checksum passes). If finalize fires but indicate doesn't, my frame's
           checksum/reassembly is wrong. */
        tm.OnPc(0xC0E22780u, [this](const TraceContext& c) {
            if (fin_++ >= 6) return;
            auto buf = c.ReadVa32(c.regs[1]);
            if (!buf) { LOG(Jit, "[IPCRX] finalize (#%d) no buf\n", fin_); return; }
            const uint32_t b = *buf;
            auto rd = [&](uint32_t o){ auto v=c.ReadVa32(b+o); return v?*v:0u; };
            /* Dump the decode-buffer control dwords v5[0..13]: seg ptr/size
               pairs (1..6), read indices (9..11), run-state lit/zr (12/13). */
            char dw[160]={0}; int off=0;
            for (uint32_t i=0;i<14;++i){ off+=std::snprintf(dw+off,sizeof(dw)-off,"%08X ", rd(i*4)); }
            LOG(Jit, "[IPCRX] finalize (#%d) buf=0x%08X dwords[0..13]: %s\n", fin_, b, dw);
        });
        tm.OnPc(0xC0E21528u, [this](const TraceContext& c) {
            if (ind_++ < 8)
                LOG(Jit, "[IPCRX] INDICATE-UP (#%d) hdrlen=%u datalen=%u\n",
                    ind_, c.regs[2], c.regs[3]);
        });
    }

private:
    int walk_ = 0;
    int fail_ = 0;
    int bfcc_ = 0;
    int resv_ = 0;
    int stat_ = 0;
    bool bsp_dumped_ = false;
    int dpll_dumps_ = 0;
    int dpll_pa_dumps_ = 0;
    int dpll_freq_dumps_ = 0;
    int irqvec_ = 0;
    int rxisr_ = 0;
    int rxsm_ = 0;
    int fin_ = 0;
    int ind_ = 0;
    int txsm_ = 0;
    int rxack_ = 0;
    int ilreg_ = 0;
    std::string dbg_line_;
};

}  /* namespace */

REGISTER_SERVICE(Sync2DpsOffsetTrace);

#endif  /* CERF_DEV_MODE */
