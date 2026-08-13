#pragma once

#include "../core/service.h"
#include "peripheral_base.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class PeripheralDispatcher : public Service {
public:
    using Service::Service;

    void Register(Peripheral* p);

    bool IsPeripheralAddress(uint32_t addr) const;

    void ValidatePhysReachable(uint32_t phys_addr_mask) const;

    std::vector<Peripheral*> RegisteredPeripherals() const;

    uint8_t  ReadByte (uint32_t addr);
    uint16_t ReadHalf (uint32_t addr);
    uint32_t ReadWord (uint32_t addr);
    uint64_t ReadDword(uint32_t addr);
    void     WriteByte (uint32_t addr, uint8_t  value);
    void     WriteHalf (uint32_t addr, uint16_t value);
    void     WriteWord (uint32_t addr, uint32_t value);
    void     WriteDword(uint32_t addr, uint64_t value);

private:
    struct Entry {
        uint32_t                base;
        uint32_t                end;      /* exclusive */
        Peripheral::FastReadFn  read;
        Peripheral::FastWriteFn write;
        void*                   ctx;
        Peripheral*             p;
    };

    using EntryTable = std::vector<Entry>;

    std::atomic<const EntryTable*>          live_{nullptr};
    std::vector<std::unique_ptr<EntryTable>> tables_;

    mutable std::atomic<size_t> last_hit_{0};

    const Entry* LookupEntry(uint32_t addr) const;
};
