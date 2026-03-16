#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace straylight {

/// CTR-DRBG (NIST SP 800-90A) using AES-256 as the block cipher.
/// Simplified portable implementation — production builds use OpenSSL EVP.
class CtrDrbg {
public:
    Result<void, SLError> seed(const std::array<uint8_t, 32>& entropy);
    Result<void, SLError> reseed(const std::array<uint8_t, 32>& additional);
    Result<std::vector<uint8_t>, SLError> generate(size_t n_bytes);

private:
    std::array<uint8_t, 32> key_{};
    std::array<uint8_t, 16> counter_{};
    bool seeded_ = false;
    mutable std::mutex mutex_;

    void update(const std::array<uint8_t, 32>& provided_data);
    void increment_counter();
};

} // namespace straylight
