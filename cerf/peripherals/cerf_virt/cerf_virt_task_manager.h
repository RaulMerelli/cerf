#pragma once

#include "../peripheral_base.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

/* Host<->guest task-manager MMIO channel for guest additions. The host UI
   enqueues commands; one command is published to the guest at a time and the
   next is published when the guest's response kick is consumed. */
class CerfVirtTaskManager : public Peripheral {
public:
    using Peripheral::Peripheral;

    struct ProcEntry {
        uint32_t     pid;
        uint32_t     parent_pid;
        uint32_t     thread_count;
        int32_t      base_priority;
        uint32_t     mem_base;
        std::wstring name;
    };
    struct Snapshot {
        uint64_t gen = 0;          /* bumps on every accepted LIST response */
        uint32_t guest_total = 0;  /* > procs.size() => guest table truncated */
        std::vector<ProcEntry> procs;
    };
    struct ActionResult {
        uint32_t ticket;
        uint32_t code;
        bool     ok;
        uint32_t guest_err;   /* guest GetLastError() when !ok */
    };

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* UI thread. Tickets identify the eventual ActionResult; 0 = rejected. */
    void     RequestProcessList();   /* deduped: at most one LIST cycle live */
    uint32_t RequestKill(uint32_t pid);
    uint32_t RequestSwitchTo(uint32_t pid);
    uint32_t RequestRun(const std::wstring& cmdline);
    Snapshot GetSnapshot();
    std::optional<ActionResult> TakeActionResult();

private:
    struct PendingCmd {
        uint32_t     ticket;
        uint32_t     code;
        uint32_t     pid;
        std::wstring run_text;
    };

    uint32_t EnqueueLocked(uint32_t code, uint32_t pid, std::wstring run_text);
    void     PublishNextLocked();
    void     ConsumeResponseLocked();

    std::mutex             mtx_;
    std::deque<PendingCmd> queue_;
    bool                   in_flight_      = false;
    uint32_t               in_flight_code_ = 0;
    bool                   list_live_      = false;  /* LIST queued or in flight */
    uint32_t               next_ticket_    = 0;

    /* Published command registers (guest-visible; cmd_gen_ is the ticket). */
    uint32_t     cmd_gen_  = 0;
    uint32_t     cmd_code_ = 0;
    uint32_t     cmd_pid_  = 0;
    std::wstring cmd_run_text_;

    /* Guest-written response registers. */
    uint32_t resp_cmd_gen_ = 0;
    uint32_t resp_status_  = 0;
    uint32_t resp_err_     = 0;
    uint32_t resp_count_   = 0;
    uint32_t resp_total_   = 0;

    /* LIST record staging: one record streamed through the window per
       kTmRecKick, accumulated until the response kick publishes them. */
    uint32_t               rec_words_[37] = {};
    uint32_t               rec_index_     = 0;
    std::vector<ProcEntry> pending_rows_;

    void ConsumeRecordLocked();

    Snapshot                    snap_;
    std::optional<ActionResult> last_action_;
};
