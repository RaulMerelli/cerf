#pragma once

#include <cstdint>
#include <vector>

class KtpMobileEmmcBacking {
public:
    virtual ~KtpMobileEmmcBacking() = default;

    virtual void Initialize(std::vector<uint8_t>& data) = 0;
    virtual void Persist(const std::vector<uint8_t>& data, uint64_t offset,
                         uint64_t length) = 0;
    virtual void Flush(const std::vector<uint8_t>& data) = 0;
};
