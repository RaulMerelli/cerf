#pragma once

#include <cstdint>

/* Guest-additions task-manager channel contract. The CE toolchain cannot
   include this header, so ce_apps/cerf_guest/cerf_task_manager_pump.cpp
   duplicates these offsets — any change here must be mirrored there or the
   channel silently desyncs. */

namespace CerfVirt {

/* Host->guest command registers. */
constexpr uint32_t kTmCmdGen     = 0x00;  /* host bumps last, after the args */
constexpr uint32_t kTmCmdCode    = 0x04;
constexpr uint32_t kTmCmdPid     = 0x08;  /* KILL / SWITCH target */
constexpr uint32_t kTmCmdRunLen  = 0x0C;  /* RUN: command line wchars, no NUL */
constexpr uint32_t kTmCmdRunText = 0x100; /* RUN: wide string, kTmRunMaxWchars cap */

/* Guest->host response registers; the guest writes kTmRespKick last. LIST
   rows arrive through the record window, one per kTmRecKick: FCSE kernels
   lazily zero L2 entries under TLB-resident pages, so guest memory is only
   reliably readable by the guest itself. */
constexpr uint32_t kTmRespCmdGen  = 0x40;  /* echo of the serviced cmd gen */
constexpr uint32_t kTmRespStatus  = 0x44;  /* 1 = success, 0 = failure */
constexpr uint32_t kTmRespErr     = 0x48;  /* guest GetLastError() on failure */
constexpr uint32_t kTmRespCount   = 0x50;  /* LIST: records streamed */
constexpr uint32_t kTmRespTotal   = 0x54;  /* LIST: snapshot total (> count = truncated) */
constexpr uint32_t kTmRecIndex    = 0x60;  /* LIST: index of the staged record */
constexpr uint32_t kTmRecKick     = 0x64;  /* LIST: host consumes the staged record */
constexpr uint32_t kTmRespKick    = 0x80;

/* Record window: the guest writes one TaskManagerProcRecord as raw words,
   then kicks kTmRecKick. */
constexpr uint32_t kTmRecData     = 0x200;

/* Command codes. */
constexpr uint32_t kTmCmdNone     = 0;
constexpr uint32_t kTmCmdList     = 1;
constexpr uint32_t kTmCmdKill     = 2;
constexpr uint32_t kTmCmdSwitchTo = 3;
constexpr uint32_t kTmCmdRun      = 4;

constexpr uint32_t kTmRunMaxWchars   = 256;
constexpr uint32_t kTmMaxProcRecords = 256;
constexpr uint32_t kTmProcNameWchars = 64;

/* One process row, filled by the guest pump from PROCESSENTRY32 (CE
   tlhelp32.h). name is NUL-terminated, truncated to kTmProcNameWchars-1;
   uint16_t keeps the 2-byte-wchar layout host-wchar_t-independent. */
struct TaskManagerProcRecord {
    uint32_t pid;
    uint32_t parent_pid;
    uint32_t thread_count;
    int32_t  base_priority;
    uint32_t mem_base;
    uint16_t name[kTmProcNameWchars];
};

static_assert(sizeof(TaskManagerProcRecord) == 148,
              "record layout must stay byte-identical to the guest pump's");

}  /* namespace CerfVirt */
