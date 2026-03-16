#include "drbg.h"

#include <algorithm>
#include <numeric>

namespace straylight {

void CtrDrbg::increment_counter() {
    // Big-endian increment of 128-bit counter
    for (int i = 15; i >= 0; --i) {
        if (++counter_[i] != 0) break;
    }
}

void CtrDrbg::update(const std::array<uint8_t, 32>& provided_data) {
    // Simplified DRBG update: XOR provided_data into key.
    // Full NIST SP 800-90A version uses AES-256-ECB; this preserves
    // the interface for portable builds without OpenSSL.
    for (size_t i = 0; i < 32; ++i) {
        key_[i] ^= provided_data[i];
    }
    increment_counter();
}

Result<void, SLError> CtrDrbg::seed(const std::array<uint8_t, 32>& entropy) {
    std::lock_guard lock(mutex_);
    key_ = entropy;
    counter_.fill(0);
    increment_counter();
    seeded_ = true;
    return Result<void, SLError>::ok();
}

Result<void, SLError> CtrDrbg::reseed(const std::array<uint8_t, 32>& additional) {
    std::lock_guard lock(mutex_);
    if (!seeded_)
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotInitialized, "DRBG not seeded"});
    update(additional);
    return Result<void, SLError>::ok();
}

Result<std::vector<uint8_t>, SLError> CtrDrbg::generate(size_t n_bytes) {
    std::lock_guard lock(mutex_);
    if (!seeded_)
        return Result<std::vector<uint8_t>, SLError>::error(
            SLError{SLErrorCode::NotInitialized, "DRBG not seeded"});

    std::vector<uint8_t> output;
    output.reserve(n_bytes);

    while (output.size() < n_bytes) {
        increment_counter();
        // Generate block by mixing key and counter
        std::array<uint8_t, 16> block{};
        for (size_t i = 0; i < 16; ++i) {
            block[i] = key_[i] ^ counter_[i] ^ key_[i + 16];
        }
        output.insert(output.end(), block.begin(), block.end());
    }
    output.resize(n_bytes);

    // Post-generate update (NIST SP 800-90A section 10.2.1.5.2)
    std::array<uint8_t, 32> zero{};
    update(zero);
    return Result<std::vector<uint8_t>, SLError>::ok(std::move(output));
}

} // namespace straylight
