# Plan 7B: Exotic Subsystems — straylight-enclave, straylight-fuse

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 2 exotic subsystem binaries: `straylight-enclave` (SGX secure inference) and `straylight-fuse` (tensor compression FUSE filesystem daemon). After this plan, the `straylight-exotic` Debian package gains its two most hardware-specialized components.

**Architecture:** Two binaries under `bin/`. `straylight-fuse` is a persistent daemon using `DaemonBase` (init/tick/shutdown) wrapping a `fuse_session`. `straylight-enclave` is an on-demand CLI tool that performs attestation, sealing, and secure inference operations. Both link `libstraylight-common`. Enclave additionally links `libstraylight-ml` + `libstraylight-hw`. Fuse additionally links `libstraylight-ml`.

**Tech Stack:** C++20, nlohmann/json 3.11+, spdlog 1.13+, GTest 1.14+, Intel SGX SDK (or stub), libfuse3, LZ4, zstd

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, libstraylight-ml, libstraylight-hw)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). SGX requires Intel hardware with SGX enabled in BIOS; graceful stub fallback when unavailable. FUSE requires libfuse3-dev and `fusermount3`.

**Error handling rules:**
- Libraries (internal modules) return `Result<T, std::string>`
- The daemon (`straylight-fuse`) translates to `Result<T, SLError>` at the DaemonBase boundary
- On-demand tool (`straylight-enclave`) translates to `Result<T, SLError>` only in `main()` for exit codes
- Use `Result<T,E>::ok(value)` / `Result<T,E>::error(err)` -- never `std::unexpected`

---

## Chunk 1: straylight-enclave -- Attestation & Sealed Storage

`bin/enclave/` -- On-demand CLI tool for SGX attestation and sealed storage. Provides local/remote attestation via EREPORT/EPID, and data sealing/unsealing bound to enclave identity. Links `libstraylight-common` + `libstraylight-hw`.

### File Structure

```
bin/enclave/
├── CMakeLists.txt
├── main.cpp
├── attestation.h
├── attestation.cpp
├── sealed_storage.h
└── sealed_storage.cpp
tests/unit/subsystems/
└── test_enclave_attestation.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_enclave_attestation.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_enclave_attestation.cpp
#include <gtest/gtest.h>
#include "attestation.h"
#include "sealed_storage.h"

using namespace straylight::enclave;

TEST(Attestation, LocalReportGenerates) {
    AttestationCtx ctx;
    auto r = ctx.init(SgxMode::Stub);
    ASSERT_TRUE(r.has_value());

    auto report = ctx.generate_local_report();
    ASSERT_TRUE(report.has_value());
    EXPECT_FALSE(report->mr_enclave.empty());
    EXPECT_FALSE(report->mr_signer.empty());
    EXPECT_GT(report->isv_svn, 0u);
}

TEST(Attestation, RemoteQuoteFromReport) {
    AttestationCtx ctx;
    ctx.init(SgxMode::Stub);
    auto report = ctx.generate_local_report();
    ASSERT_TRUE(report.has_value());

    auto quote = ctx.generate_remote_quote(*report);
    ASSERT_TRUE(quote.has_value());
    EXPECT_FALSE(quote->data.empty());
}

TEST(Attestation, VerifyQuoteRoundtrip) {
    AttestationCtx ctx;
    ctx.init(SgxMode::Stub);
    auto report = ctx.generate_local_report();
    auto quote  = ctx.generate_remote_quote(*report);
    auto ok     = ctx.verify_quote(*quote);
    EXPECT_TRUE(ok.has_value());
    EXPECT_TRUE(*ok);
}

TEST(SealedStorage, SealUnsealRoundtrip) {
    SealedStore store;
    store.init(SgxMode::Stub);

    std::vector<uint8_t> plaintext = {0xDE, 0xAD, 0xBE, 0xEF};
    auto sealed = store.seal(plaintext, SealPolicy::MrEnclave);
    ASSERT_TRUE(sealed.has_value());
    EXPECT_NE(sealed->ciphertext, plaintext);

    auto unsealed = store.unseal(*sealed);
    ASSERT_TRUE(unsealed.has_value());
    EXPECT_EQ(*unsealed, plaintext);
}

TEST(SealedStorage, UnsealWrongPolicyFails) {
    SealedStore store;
    store.init(SgxMode::Stub);

    std::vector<uint8_t> data = {1, 2, 3, 4};
    auto sealed = store.seal(data, SealPolicy::MrEnclave);
    ASSERT_TRUE(sealed.has_value());

    // Corrupt seal tag
    sealed->tag[0] ^= 0xFF;
    auto bad = store.unseal(*sealed);
    EXPECT_FALSE(bad.has_value());
}
```

### Task 2: attestation.h/cpp

**Files:** `bin/enclave/attestation.h`, `bin/enclave/attestation.cpp`

- [ ] **Step 1: Create attestation.h**

```cpp
// bin/enclave/attestation.h
#pragma once
#include <straylight/types.h>
#include <cstdint>
#include <vector>
#include <string>

namespace straylight::enclave {

enum class SgxMode { Hardware, Stub };

struct LocalReport {
    std::string mr_enclave;   // SHA-256 of enclave measurement
    std::string mr_signer;    // SHA-256 of signing key
    uint16_t    isv_svn{0};   // ISV security version
    std::vector<uint8_t> report_data;
};

struct RemoteQuote {
    std::vector<uint8_t> data;
    std::string          epid_group_id;
};

class AttestationCtx {
public:
    Result<void, std::string> init(SgxMode mode);
    Result<LocalReport, std::string> generate_local_report();
    Result<RemoteQuote, std::string> generate_remote_quote(const LocalReport& report);
    Result<bool, std::string> verify_quote(const RemoteQuote& quote);

private:
    SgxMode mode_{SgxMode::Stub};
    bool    initialized_{false};

    // Hardware path: wraps sgx_create_report / sgx_get_quote
    Result<LocalReport, std::string> hw_report();
    Result<RemoteQuote, std::string> hw_quote(const LocalReport& r);

    // Stub path: deterministic fake values for testing
    Result<LocalReport, std::string> stub_report();
    Result<RemoteQuote, std::string> stub_quote(const LocalReport& r);
};

} // namespace straylight::enclave
```

- [ ] **Step 2: Create attestation.cpp**

```cpp
// bin/enclave/attestation.cpp
#include "attestation.h"
#include <straylight/log.h>
#include <openssl/sha.h>    // SHA256 for stub measurement
#include <cstring>

namespace straylight::enclave {

Result<void, std::string> AttestationCtx::init(SgxMode mode) {
    mode_ = mode;
    if (mode == SgxMode::Hardware) {
        // Attempt to open /dev/sgx_enclave
        int fd = ::open("/dev/sgx_enclave", O_RDWR);
        if (fd < 0) return Result<void, std::string>::error("SGX device unavailable");
        ::close(fd);
    }
    initialized_ = true;
    SL_INFO("attestation init mode={}", mode == SgxMode::Hardware ? "hw" : "stub");
    return Result<void, std::string>::ok({});
}

Result<LocalReport, std::string> AttestationCtx::generate_local_report() {
    if (!initialized_) return Result<LocalReport, std::string>::error("not initialized");
    return (mode_ == SgxMode::Hardware) ? hw_report() : stub_report();
}

// ... generate_remote_quote, verify_quote delegate similarly ...

Result<LocalReport, std::string> AttestationCtx::stub_report() {
    LocalReport r;
    r.mr_enclave = "stub_mrenclave_" + std::string(48, 'a');  // 64 chars
    r.mr_signer  = "stub_mrsigner_"  + std::string(49, 'b');
    r.isv_svn    = 1;
    r.report_data.resize(64, 0x42);
    return Result<LocalReport, std::string>::ok(std::move(r));
}

Result<RemoteQuote, std::string> AttestationCtx::stub_quote(const LocalReport& r) {
    RemoteQuote q;
    q.epid_group_id = "stub-epid-0001";
    // Encode report fields into quote data
    q.data.insert(q.data.end(), r.report_data.begin(), r.report_data.end());
    q.data.push_back(0xFF);  // sentinel
    return Result<RemoteQuote, std::string>::ok(std::move(q));
}

// Hardware paths: wrap SGX SDK calls (sgx_create_report, sgx_init_quote, sgx_get_quote)
// Omitted for brevity -- mirrors stub structure but calls into libsgx_urts

} // namespace straylight::enclave
```

### Task 3: sealed_storage.h/cpp

**Files:** `bin/enclave/sealed_storage.h`, `bin/enclave/sealed_storage.cpp`

- [ ] **Step 1: Create sealed_storage.h**

```cpp
// bin/enclave/sealed_storage.h
#pragma once
#include <straylight/types.h>
#include "attestation.h"
#include <vector>
#include <cstdint>

namespace straylight::enclave {

enum class SealPolicy { MrEnclave, MrSigner };

struct SealedBlob {
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> tag;       // 16-byte AES-GCM tag
    std::vector<uint8_t> nonce;     // 12-byte nonce
    SealPolicy           policy;
};

class SealedStore {
public:
    Result<void, std::string> init(SgxMode mode);
    Result<SealedBlob, std::string> seal(std::span<const uint8_t> plaintext, SealPolicy policy);
    Result<std::vector<uint8_t>, std::string> unseal(const SealedBlob& blob);

private:
    SgxMode mode_{SgxMode::Stub};
    std::vector<uint8_t> enclave_key_;  // Derived sealing key (or stub)

    void derive_stub_key();
};

} // namespace straylight::enclave
```

- [ ] **Step 2: Create sealed_storage.cpp**

Core logic -- AES-256-GCM seal/unseal using enclave-derived key:

```cpp
// bin/enclave/sealed_storage.cpp
#include "sealed_storage.h"
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace straylight::enclave {

Result<void, std::string> SealedStore::init(SgxMode mode) {
    mode_ = mode;
    if (mode == SgxMode::Hardware) {
        // sgx_get_key() with key_policy based on MR_ENCLAVE/MR_SIGNER
        // ... hardware path omitted ...
    } else {
        derive_stub_key();
    }
    return Result<void, std::string>::ok({});
}

void SealedStore::derive_stub_key() {
    enclave_key_.resize(32, 0xAB);  // Deterministic stub key
}

Result<SealedBlob, std::string> SealedStore::seal(
        std::span<const uint8_t> plaintext, SealPolicy policy) {
    SealedBlob blob{.policy = policy};
    blob.nonce.resize(12);
    RAND_bytes(blob.nonce.data(), 12);
    blob.tag.resize(16);
    blob.ciphertext.resize(plaintext.size());

    auto* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                       enclave_key_.data(), blob.nonce.data());
    int len = 0;
    EVP_EncryptUpdate(ctx, blob.ciphertext.data(), &len,
                      plaintext.data(), plaintext.size());
    EVP_EncryptFinal_ex(ctx, blob.ciphertext.data() + len, &len);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, blob.tag.data());
    EVP_CIPHER_CTX_free(ctx);
    return Result<SealedBlob, std::string>::ok(std::move(blob));
}

// unseal: reverse -- EVP_DecryptInit_ex, set tag, decrypt, check final
// Returns error on tag mismatch

} // namespace straylight::enclave
```

### Task 4: CMakeLists.txt + main.cpp

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
# bin/enclave/CMakeLists.txt
add_executable(straylight-enclave main.cpp attestation.cpp sealed_storage.cpp)
target_link_libraries(straylight-enclave PRIVATE
    straylight-common straylight-ml straylight-hw OpenSSL::Crypto)

# Optional SGX SDK -- graceful fallback
find_package(SGX QUIET)
if(SGX_FOUND)
    target_compile_definitions(straylight-enclave PRIVATE STRAYLIGHT_SGX_HW=1)
    target_link_libraries(straylight-enclave PRIVATE SGX::urts SGX::uae_service)
endif()
install(TARGETS straylight-enclave RUNTIME DESTINATION bin)
```

- [ ] **Step 2: Create main.cpp**

```cpp
// bin/enclave/main.cpp
#include "attestation.h"
#include "sealed_storage.h"
#include "secure_inference.h"
#include <straylight/config.h>
#include <straylight/log.h>
#include <iostream>

using namespace straylight::enclave;

int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/enclave.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-enclave", cfg.log_level());

    if (argc < 2) {
        std::cerr << "Usage: straylight-enclave <attest|seal|unseal|infer>\n";
        return 1;
    }
    std::string cmd = argv[1];
    auto mode = SgxMode::Stub;
#ifdef STRAYLIGHT_SGX_HW
    mode = SgxMode::Hardware;
#endif

    if (cmd == "attest") {
        AttestationCtx ctx;
        if (auto r = ctx.init(mode); !r) { SL_ERROR("{}", r.error()); return 1; }
        auto report = ctx.generate_local_report();
        if (!report) { SL_ERROR("{}", report.error()); return 1; }
        auto quote = ctx.generate_remote_quote(*report);
        if (!quote) { SL_ERROR("{}", quote.error()); return 1; }
        SL_INFO("attestation OK, quote_size={}", quote->data.size());
    } else if (cmd == "seal" || cmd == "unseal") {
        // Read from stdin, write to stdout
        SealedStore store;
        if (auto r = store.init(mode); !r) { SL_ERROR("{}", r.error()); return 1; }
        // ... stdin/stdout piping omitted for brevity ...
    } else if (cmd == "infer") {
        SecureInference eng;
        if (auto r = eng.init(mode, cfg); !r) { SL_ERROR("{}", r.error()); return 1; }
        // ... model path from argv[2], input from stdin ...
    }
    return 0;
}
```

---

## Chunk 2: straylight-enclave -- Secure Inference

`bin/enclave/` continued -- Run ML inference inside an SGX enclave. Encrypted model is loaded, decrypted within enclave boundary, inference runs, output is re-encrypted before leaving.

### File Structure (additions)

```
bin/enclave/
├── secure_inference.h
├── secure_inference.cpp
└── enclave_def/
    ├── enclave.edl         # EDL interface definition
    └── enclave.cpp         # Trusted code (runs inside enclave)
```

### Task 1: secure_inference.h/cpp

**Files:** `bin/enclave/secure_inference.h`, `bin/enclave/secure_inference.cpp`

- [ ] **Step 1: Create secure_inference.h**

```cpp
// bin/enclave/secure_inference.h
#pragma once
#include <straylight/types.h>
#include "attestation.h"
#include "sealed_storage.h"
#include <filesystem>
#include <vector>

namespace straylight::enclave {

struct InferenceRequest {
    std::filesystem::path encrypted_model;
    std::vector<uint8_t>  input_tensor;
};

struct InferenceResult {
    std::vector<uint8_t> encrypted_output;
    uint64_t             latency_us;
};

class SecureInference {
public:
    Result<void, std::string> init(SgxMode mode, const Config& cfg);
    Result<InferenceResult, std::string> run(const InferenceRequest& req);
    void teardown();

private:
    SealedStore sealer_;
    SgxMode     mode_;

    Result<std::vector<uint8_t>, std::string> load_and_decrypt_model(
        const std::filesystem::path& path);
    Result<std::vector<uint8_t>, std::string> execute_graph(
        std::span<const uint8_t> model, std::span<const uint8_t> input);
};

} // namespace straylight::enclave
```

- [ ] **Step 2: Create secure_inference.cpp**

```cpp
// bin/enclave/secure_inference.cpp
#include "secure_inference.h"
#include <straylight/log.h>
#include <chrono>
#include <fstream>

namespace straylight::enclave {

Result<void, std::string> SecureInference::init(SgxMode mode, const Config& cfg) {
    mode_ = mode;
    return sealer_.init(mode);
}

Result<InferenceResult, std::string> SecureInference::run(const InferenceRequest& req) {
    auto t0 = std::chrono::steady_clock::now();

    // 1. Load encrypted model, unseal inside enclave boundary
    auto model = load_and_decrypt_model(req.encrypted_model);
    if (!model) return Result<InferenceResult, std::string>::error(model.error());

    // 2. Execute graph on plaintext model + input
    auto output = execute_graph(*model, req.input_tensor);
    if (!output) return Result<InferenceResult, std::string>::error(output.error());

    // 3. Re-encrypt output before leaving enclave
    auto sealed = sealer_.seal(*output, SealPolicy::MrEnclave);
    if (!sealed) return Result<InferenceResult, std::string>::error(sealed.error());

    auto dt = std::chrono::steady_clock::now() - t0;
    return Result<InferenceResult, std::string>::ok({
        .encrypted_output = std::move(sealed->ciphertext),
        .latency_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(dt).count())
    });
}

Result<std::vector<uint8_t>, std::string> SecureInference::load_and_decrypt_model(
        const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Result<std::vector<uint8_t>, std::string>::error("cannot open model");
    std::vector<uint8_t> enc(std::istreambuf_iterator<char>(f), {});

    // Parse as SealedBlob (header: 12-byte nonce + 16-byte tag + ciphertext)
    if (enc.size() < 28) return Result<std::vector<uint8_t>, std::string>::error("model too small");
    SealedBlob blob;
    blob.nonce.assign(enc.begin(), enc.begin() + 12);
    blob.tag.assign(enc.begin() + 12, enc.begin() + 28);
    blob.ciphertext.assign(enc.begin() + 28, enc.end());
    blob.policy = SealPolicy::MrEnclave;
    return sealer_.unseal(blob);
}

Result<std::vector<uint8_t>, std::string> SecureInference::execute_graph(
        std::span<const uint8_t> model, std::span<const uint8_t> input) {
    // In hardware mode: ecall into enclave.cpp trusted code
    // In stub mode: simple passthrough (XOR with model[0] for testing)
    std::vector<uint8_t> out(input.begin(), input.end());
    if (!model.empty()) {
        for (auto& b : out) b ^= model[0];  // Stub "inference"
    }
    return Result<std::vector<uint8_t>, std::string>::ok(std::move(out));
}

} // namespace straylight::enclave
```

### Task 2: Enclave definition (EDL + trusted code)

- [ ] **Step 1: Create enclave.edl**

```c
// bin/enclave/enclave_def/enclave.edl
enclave {
    trusted {
        public int ecall_load_model(
            [in, size=model_size] const uint8_t* encrypted_model,
            size_t model_size);
        public int ecall_infer(
            [in, size=in_size]  const uint8_t* input, size_t in_size,
            [out, size=out_size] uint8_t* output, size_t out_size);
    };
    untrusted {
        void ocall_log([in, string] const char* msg);
    };
};
```

- [ ] **Step 2: Create enclave.cpp (trusted)**

```cpp
// bin/enclave/enclave_def/enclave.cpp  (runs inside SGX enclave)
#include "enclave_t.h"   // Generated from EDL
#include <cstring>

static uint8_t* g_model = nullptr;
static size_t   g_model_size = 0;

int ecall_load_model(const uint8_t* encrypted_model, size_t model_size) {
    // Decrypt using enclave seal key (sgx_unseal_data)
    // Store plaintext in enclave heap
    g_model = new uint8_t[model_size];
    g_model_size = model_size;
    std::memcpy(g_model, encrypted_model, model_size); // Placeholder
    return 0;
}

int ecall_infer(const uint8_t* input, size_t in_size,
                uint8_t* output, size_t out_size) {
    if (!g_model || out_size < in_size) return -1;
    // Simple tensor op inside enclave (real: run quantized graph)
    for (size_t i = 0; i < in_size; ++i)
        output[i] = input[i] ^ g_model[i % g_model_size];
    return 0;
}
```

---

## Chunk 3: straylight-fuse -- FUSE Daemon + Tensor Format + Compression

`bin/fuse/` -- Tensor compression FUSE filesystem. Presents a virtual filesystem where tensor files are stored compressed on disk (LZ4/zstd with sparsity exploitation) and decompressed transparently on read. Persistent daemon using `DaemonBase`.

### File Structure

```
bin/fuse/
├── CMakeLists.txt
├── main.cpp
├── fuse_daemon.h
├── fuse_daemon.cpp
├── operations.h
├── operations.cpp
├── compression.h
├── compression.cpp
├── tensor_format.h
├── tensor_format.cpp
├── cache.h
└── cache.cpp
tests/unit/subsystems/
├── test_fuse_compression.cpp
└── test_fuse_cache.cpp
etc/systemd/system/
└── straylight-fuse.service
```

### Task 1: tensor_format.h/cpp -- On-disk tensor format

**Files:** `bin/fuse/tensor_format.h`, `bin/fuse/tensor_format.cpp`

- [ ] **Step 1: Create tensor_format.h**

```cpp
// bin/fuse/tensor_format.h
#pragma once
#include <straylight/types.h>
#include <cstdint>
#include <vector>
#include <string>
#include <span>

namespace straylight::fuse {

constexpr uint32_t TENSOR_MAGIC = 0x544E5352; // "TNSR"
constexpr uint32_t TENSOR_VERSION = 1;
constexpr size_t   BLOCK_SIZE = 256 * 1024;   // 256 KiB blocks

enum class DType : uint8_t { F32 = 0, F16, BF16, I8, I4 };
enum class Codec : uint8_t { None = 0, LZ4, Zstd };

struct TensorHeader {
    uint32_t magic{TENSOR_MAGIC};
    uint32_t version{TENSOR_VERSION};
    DType    dtype{DType::F32};
    Codec    codec{Codec::Zstd};
    uint32_t ndim{0};
    uint64_t total_elements{0};
    uint32_t num_blocks{0};
    std::vector<uint64_t> shape;
};

struct BlockIndex {
    uint64_t offset;          // Byte offset in file
    uint32_t compressed_size;
    uint32_t original_size;
    float    sparsity;        // Fraction of zeros (0.0-1.0)
};

// Serialize/deserialize header + block index
Result<std::vector<uint8_t>, std::string> serialize_header(
    const TensorHeader& hdr, const std::vector<BlockIndex>& blocks);
Result<std::pair<TensorHeader, std::vector<BlockIndex>>, std::string>
    parse_header(std::span<const uint8_t> data);

} // namespace straylight::fuse
```

- [ ] **Step 2: Create tensor_format.cpp**

```cpp
// bin/fuse/tensor_format.cpp
#include "tensor_format.h"
#include <cstring>

namespace straylight::fuse {

Result<std::vector<uint8_t>, std::string> serialize_header(
        const TensorHeader& hdr, const std::vector<BlockIndex>& blocks) {
    // Layout: [magic:4][ver:4][dtype:1][codec:1][ndim:4][elems:8][nblocks:4]
    //         [shape: ndim*8][block_index: nblocks*24]
    size_t hdr_size = 26 + hdr.ndim * 8 + blocks.size() * 24;
    std::vector<uint8_t> buf(hdr_size);
    uint8_t* p = buf.data();

    auto put32 = [&](uint32_t v) { std::memcpy(p, &v, 4); p += 4; };
    auto put64 = [&](uint64_t v) { std::memcpy(p, &v, 8); p += 8; };
    put32(hdr.magic); put32(hdr.version);
    *p++ = static_cast<uint8_t>(hdr.dtype);
    *p++ = static_cast<uint8_t>(hdr.codec);
    put32(hdr.ndim); put64(hdr.total_elements); put32(hdr.num_blocks);
    for (auto d : hdr.shape) put64(d);
    for (auto& b : blocks) {
        put64(b.offset); put32(b.compressed_size);
        put32(b.original_size);
        float s = b.sparsity; std::memcpy(p, &s, 4); p += 4;
    }
    return Result<std::vector<uint8_t>, std::string>::ok(std::move(buf));
}

// parse_header: inverse of above, validates magic + version

} // namespace straylight::fuse
```

### Task 2: compression.h/cpp -- LZ4/zstd tensor-aware compression

**Files:** `bin/fuse/compression.h`, `bin/fuse/compression.cpp`

- [ ] **Step 1: Create compression.h**

```cpp
// bin/fuse/compression.h
#pragma once
#include <straylight/types.h>
#include "tensor_format.h"
#include <span>
#include <vector>

namespace straylight::fuse {

struct CompressionStats {
    size_t original_bytes;
    size_t compressed_bytes;
    float  sparsity;
};

class TensorCompressor {
public:
    Result<std::pair<std::vector<uint8_t>, CompressionStats>, std::string>
        compress(std::span<const uint8_t> block, Codec codec, DType dtype);

    Result<std::vector<uint8_t>, std::string>
        decompress(std::span<const uint8_t> compressed, Codec codec,
                   uint32_t original_size);

private:
    float compute_sparsity(std::span<const uint8_t> data, DType dtype);
    // Pre-filter: pack sparse tensors before compression
    std::vector<uint8_t> sparse_prefilter(std::span<const uint8_t> data, DType dtype);
};

} // namespace straylight::fuse
```

- [ ] **Step 2: Create compression.cpp**

```cpp
// bin/fuse/compression.cpp
#include "compression.h"
#include <lz4.h>
#include <zstd.h>
#include <cstring>

namespace straylight::fuse {

float TensorCompressor::compute_sparsity(std::span<const uint8_t> data, DType dtype) {
    size_t elem_size = (dtype == DType::F32) ? 4 : (dtype == DType::F16) ? 2 : 1;
    size_t n_elems = data.size() / elem_size;
    size_t zeros = 0;
    for (size_t i = 0; i < n_elems; ++i) {
        bool is_zero = true;
        for (size_t b = 0; b < elem_size; ++b)
            if (data[i * elem_size + b] != 0) { is_zero = false; break; }
        if (is_zero) ++zeros;
    }
    return n_elems ? static_cast<float>(zeros) / n_elems : 0.0f;
}

Result<std::pair<std::vector<uint8_t>, CompressionStats>, std::string>
TensorCompressor::compress(std::span<const uint8_t> block, Codec codec, DType dtype) {
    float sparsity = compute_sparsity(block, dtype);
    auto filtered = (sparsity > 0.5f) ? sparse_prefilter(block, dtype)
                                       : std::vector<uint8_t>(block.begin(), block.end());
    std::vector<uint8_t> out;

    if (codec == Codec::LZ4) {
        out.resize(LZ4_compressBound(filtered.size()));
        int sz = LZ4_compress_default(
            reinterpret_cast<const char*>(filtered.data()),
            reinterpret_cast<char*>(out.data()),
            filtered.size(), out.size());
        if (sz <= 0) return Result<std::pair<std::vector<uint8_t>, CompressionStats>, std::string>::error("LZ4 compress failed");
        out.resize(sz);
    } else { // Zstd
        out.resize(ZSTD_compressBound(filtered.size()));
        size_t sz = ZSTD_compress(out.data(), out.size(),
                                  filtered.data(), filtered.size(), 3);
        if (ZSTD_isError(sz)) return Result<std::pair<std::vector<uint8_t>, CompressionStats>, std::string>::error("zstd compress failed");
        out.resize(sz);
    }

    CompressionStats stats{block.size(), out.size(), sparsity};
    return Result<std::pair<std::vector<uint8_t>, CompressionStats>, std::string>::ok({std::move(out), stats});
}

// decompress: LZ4_decompress_safe / ZSTD_decompress, reverse sparse_prefilter if needed

std::vector<uint8_t> TensorCompressor::sparse_prefilter(
        std::span<const uint8_t> data, DType dtype) {
    // Run-length encode zero runs: [nonzero_count:u16][data...][zero_run:u16]...
    // This groups zeros together for better LZ4/zstd ratios
    std::vector<uint8_t> out;
    out.reserve(data.size());
    // ... RLE encoding of zero-element runs ...
    return out;
}

} // namespace straylight::fuse
```

### Task 3: cache.h/cpp -- LRU read cache

**Files:** `bin/fuse/cache.h`, `bin/fuse/cache.cpp`

- [ ] **Step 1: Create cache.h**

```cpp
// bin/fuse/cache.h
#pragma once
#include <straylight/types.h>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace straylight::fuse {

struct CacheKey {
    uint64_t inode;
    uint32_t block_idx;
    bool operator==(const CacheKey&) const = default;
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const {
        return std::hash<uint64_t>{}(k.inode) ^ (std::hash<uint32_t>{}(k.block_idx) << 32);
    }
};

class BlockCache {
public:
    explicit BlockCache(size_t max_bytes);

    Result<std::span<const uint8_t>, std::string> get(CacheKey key);
    void put(CacheKey key, std::vector<uint8_t> data);
    void evict(uint64_t inode);  // Evict all blocks for an inode
    size_t current_bytes() const { return current_bytes_; }

private:
    size_t max_bytes_;
    size_t current_bytes_{0};
    std::mutex mu_;

    struct Entry { CacheKey key; std::vector<uint8_t> data; };
    std::list<Entry> lru_;
    std::unordered_map<CacheKey, std::list<Entry>::iterator, CacheKeyHash> map_;

    void evict_lru();
};

} // namespace straylight::fuse
```

- [ ] **Step 2: Create cache.cpp**

```cpp
// bin/fuse/cache.cpp
#include "cache.h"

namespace straylight::fuse {

BlockCache::BlockCache(size_t max_bytes) : max_bytes_(max_bytes) {}

Result<std::span<const uint8_t>, std::string> BlockCache::get(CacheKey key) {
    std::lock_guard lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end())
        return Result<std::span<const uint8_t>, std::string>::error("miss");
    lru_.splice(lru_.begin(), lru_, it->second); // Move to front
    return Result<std::span<const uint8_t>, std::string>::ok(
        std::span<const uint8_t>(it->second->data));
}

void BlockCache::put(CacheKey key, std::vector<uint8_t> data) {
    std::lock_guard lk(mu_);
    if (auto it = map_.find(key); it != map_.end()) {
        current_bytes_ -= it->second->data.size();
        lru_.erase(it->second);
        map_.erase(it);
    }
    while (current_bytes_ + data.size() > max_bytes_ && !lru_.empty())
        evict_lru();
    current_bytes_ += data.size();
    lru_.push_front({key, std::move(data)});
    map_[key] = lru_.begin();
}

void BlockCache::evict_lru() {
    auto& back = lru_.back();
    current_bytes_ -= back.data.size();
    map_.erase(back.key);
    lru_.pop_back();
}

void BlockCache::evict(uint64_t inode) {
    std::lock_guard lk(mu_);
    for (auto it = lru_.begin(); it != lru_.end(); ) {
        if (it->key.inode == inode) {
            current_bytes_ -= it->data.size();
            map_.erase(it->key);
            it = lru_.erase(it);
        } else { ++it; }
    }
}

} // namespace straylight::fuse
```

---

## Chunk 4: straylight-fuse -- FUSE Operations, Daemon, Tests for All

`bin/fuse/` continued -- FUSE low-level operations, DaemonBase subclass, systemd service, main.cpp, CMakeLists.txt, and tests for both enclave and fuse subsystems.

### Task 1: operations.h/cpp -- FUSE low-level ops

**Files:** `bin/fuse/operations.h`, `bin/fuse/operations.cpp`

- [ ] **Step 1: Create operations.h**

```cpp
// bin/fuse/operations.h
#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>
#include "tensor_format.h"
#include "compression.h"
#include "cache.h"
#include <straylight/types.h>
#include <filesystem>
#include <unordered_map>
#include <shared_mutex>

namespace straylight::fuse {

struct TensorFile {
    uint64_t                  inode;
    std::string               name;
    TensorHeader              header;
    std::vector<BlockIndex>   blocks;
    std::filesystem::path     backing_path; // Compressed on-disk path
    uint64_t                  apparent_size; // Uncompressed size
};

class FuseOps {
public:
    Result<void, std::string> init(const std::filesystem::path& store_dir,
                                   size_t cache_bytes);
    // FUSE callbacks (static trampolines call into singleton)
    static void ll_lookup(fuse_req_t req, fuse_ino_t parent, const char* name);
    static void ll_getattr(fuse_req_t req, fuse_ino_t ino, fuse_file_info* fi);
    static void ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                        off_t off, fuse_file_info* fi);
    static void ll_write(fuse_req_t req, fuse_ino_t ino, const char* buf,
                         size_t size, off_t off, fuse_file_info* fi);
    static void ll_open(fuse_req_t req, fuse_ino_t ino, fuse_file_info* fi);
    static void ll_release(fuse_req_t req, fuse_ino_t ino, fuse_file_info* fi);
    static void ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                           off_t off, fuse_file_info* fi);

    static const fuse_lowlevel_ops& get_ops();

private:
    std::filesystem::path store_dir_;
    BlockCache            cache_{0};
    TensorCompressor      compressor_;
    std::shared_mutex      mu_;
    std::unordered_map<uint64_t, TensorFile> inodes_;
    uint64_t next_ino_{2}; // 1 = root

    Result<std::vector<uint8_t>, std::string> read_block(
        const TensorFile& tf, uint32_t block_idx);
};

} // namespace straylight::fuse
```

- [ ] **Step 2: Create operations.cpp**

```cpp
// bin/fuse/operations.cpp
#include "operations.h"
#include <straylight/log.h>
#include <fstream>
#include <cstring>

namespace straylight::fuse {

static FuseOps* g_ops = nullptr; // Set during init

Result<void, std::string> FuseOps::init(
        const std::filesystem::path& store_dir, size_t cache_bytes) {
    store_dir_ = store_dir;
    cache_ = BlockCache(cache_bytes);
    g_ops = this;
    // Scan store_dir for existing .tnsr files, populate inodes_
    for (auto& entry : std::filesystem::directory_iterator(store_dir)) {
        if (entry.path().extension() == ".tnsr") {
            // Read header, register inode
            // ... abbreviated: read file, parse_header, insert into inodes_ ...
        }
    }
    return Result<void, std::string>::ok({});
}

void FuseOps::ll_getattr(fuse_req_t req, fuse_ino_t ino, fuse_file_info*) {
    struct stat st{};
    if (ino == 1) {
        st.st_ino = 1; st.st_mode = S_IFDIR | 0755; st.st_nlink = 2;
        fuse_reply_attr(req, &st, 1.0);
        return;
    }
    std::shared_lock lk(g_ops->mu_);
    auto it = g_ops->inodes_.find(ino);
    if (it == g_ops->inodes_.end()) { fuse_reply_err(req, ENOENT); return; }
    st.st_ino = ino; st.st_mode = S_IFREG | 0644; st.st_nlink = 1;
    st.st_size = it->second.apparent_size;
    fuse_reply_attr(req, &st, 1.0);
}

void FuseOps::ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                       off_t off, fuse_file_info*) {
    std::shared_lock lk(g_ops->mu_);
    auto it = g_ops->inodes_.find(ino);
    if (it == g_ops->inodes_.end()) { fuse_reply_err(req, ENOENT); return; }

    const auto& tf = it->second;
    uint32_t block_idx = off / BLOCK_SIZE;
    size_t block_off   = off % BLOCK_SIZE;

    auto data = g_ops->read_block(tf, block_idx);
    if (!data) { fuse_reply_err(req, EIO); return; }

    size_t avail = data->size() > block_off ? data->size() - block_off : 0;
    size_t n = std::min(size, avail);
    fuse_reply_buf(req, reinterpret_cast<const char*>(data->data() + block_off), n);
}

Result<std::vector<uint8_t>, std::string> FuseOps::read_block(
        const TensorFile& tf, uint32_t block_idx) {
    CacheKey ck{tf.inode, block_idx};
    if (auto hit = cache_.get(ck); hit) {
        return Result<std::vector<uint8_t>, std::string>::ok(
            std::vector<uint8_t>(hit->begin(), hit->end()));
    }
    // Read compressed block from backing file
    if (block_idx >= tf.blocks.size())
        return Result<std::vector<uint8_t>, std::string>::error("block out of range");
    const auto& bi = tf.blocks[block_idx];
    std::ifstream f(tf.backing_path, std::ios::binary);
    f.seekg(bi.offset);
    std::vector<uint8_t> compressed(bi.compressed_size);
    f.read(reinterpret_cast<char*>(compressed.data()), bi.compressed_size);

    auto decompressed = compressor_.decompress(compressed, tf.header.codec, bi.original_size);
    if (!decompressed) return decompressed;
    cache_.put(ck, *decompressed);
    return decompressed;
}

// ll_lookup, ll_write, ll_open, ll_release, ll_readdir follow same pattern
// ll_write: compress block, update backing file + block index, evict cache

const fuse_lowlevel_ops& FuseOps::get_ops() {
    static fuse_lowlevel_ops ops{};
    ops.lookup  = ll_lookup;
    ops.getattr = ll_getattr;
    ops.read    = ll_read;
    ops.write   = ll_write;
    ops.open    = ll_open;
    ops.release = ll_release;
    ops.readdir = ll_readdir;
    return ops;
}

} // namespace straylight::fuse
```

### Task 2: fuse_daemon.h/cpp -- DaemonBase subclass

**Files:** `bin/fuse/fuse_daemon.h`, `bin/fuse/fuse_daemon.cpp`

- [ ] **Step 1: Create fuse_daemon.h**

```cpp
// bin/fuse/fuse_daemon.h
#pragma once
#include <straylight/daemon.h>
#include "operations.h"

namespace straylight::fuse {

class FuseDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

private:
    FuseOps              ops_;
    struct fuse_session* session_{nullptr};
    std::filesystem::path mountpoint_;
    std::filesystem::path store_dir_;
};

} // namespace straylight::fuse
```

- [ ] **Step 2: Create fuse_daemon.cpp**

```cpp
// bin/fuse/fuse_daemon.cpp
#include "fuse_daemon.h"
#include <straylight/log.h>

namespace straylight::fuse {

Result<void, SLError> FuseDaemon::init(const Config& cfg) {
    mountpoint_ = cfg.get_string("fuse.mountpoint", "/var/lib/straylight/tensors");
    store_dir_  = cfg.get_string("fuse.store_dir", "/var/lib/straylight/tensor-store");
    size_t cache_mb = cfg.get_uint("fuse.cache_mb", 512);

    std::filesystem::create_directories(mountpoint_);
    std::filesystem::create_directories(store_dir_);

    if (auto r = ops_.init(store_dir_, cache_mb * 1024 * 1024); !r)
        return Result<void, SLError>::error(SLError{SLErrorCode::InitFailed, r.error()});

    // Create FUSE session
    fuse_args args = FUSE_ARGS_INIT(0, nullptr);
    session_ = fuse_session_new(&args, &FuseOps::get_ops(),
                                 sizeof(fuse_lowlevel_ops), nullptr);
    if (!session_)
        return Result<void, SLError>::error(SLError{SLErrorCode::InitFailed, "fuse_session_new failed"});

    if (fuse_session_mount(session_, mountpoint_.c_str()) != 0) {
        fuse_session_destroy(session_);
        session_ = nullptr;
        return Result<void, SLError>::error(SLError{SLErrorCode::InitFailed, "fuse mount failed"});
    }

    SL_INFO("fuse mounted at {} (store={}, cache={}MB)",
            mountpoint_.string(), store_dir_.string(), cache_mb);
    return Result<void, SLError>::ok({});
}

Result<void, SLError> FuseDaemon::tick() {
    // Process one FUSE request (non-blocking with timeout)
    struct fuse_buf fbuf{};
    int res = fuse_session_receive_buf(session_, &fbuf);
    if (res > 0) {
        fuse_session_process_buf(session_, &fbuf);
        free(fbuf.mem);
    } else if (res < 0 && res != -EINTR) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError, "fuse recv error"});
    }
    return Result<void, SLError>::ok({});
}

void FuseDaemon::shutdown() {
    if (session_) {
        fuse_session_unmount(session_);
        fuse_session_destroy(session_);
        session_ = nullptr;
    }
    SL_INFO("fuse unmounted {}", mountpoint_.string());
}

} // namespace straylight::fuse
```

### Task 3: main.cpp + CMakeLists.txt + systemd service

- [ ] **Step 1: Create main.cpp**

```cpp
// bin/fuse/main.cpp
#include "fuse_daemon.h"
#include <straylight/config.h>
#include <straylight/log.h>

int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/fuse.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-fuse", cfg.log_level());
    straylight::fuse::FuseDaemon daemon;
    return daemon.run(cfg);
}
```

- [ ] **Step 2: Create CMakeLists.txt**

```cmake
# bin/fuse/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)

add_executable(straylight-fuse
    main.cpp fuse_daemon.cpp operations.cpp
    compression.cpp tensor_format.cpp cache.cpp)
target_include_directories(straylight-fuse PRIVATE ${FUSE3_INCLUDE_DIRS})
target_link_libraries(straylight-fuse PRIVATE
    straylight-common straylight-ml
    ${FUSE3_LIBRARIES} lz4 zstd)
target_compile_definitions(straylight-fuse PRIVATE FUSE_USE_VERSION=35)
install(TARGETS straylight-fuse RUNTIME DESTINATION bin)
```

- [ ] **Step 3: Create systemd service**

```ini
# etc/systemd/system/straylight-fuse.service
[Unit]
Description=StrayLight Tensor Compression FUSE Filesystem
After=straylight-bus.service
Requires=straylight-bus.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-fuse
ExecStop=/bin/fusermount3 -u /var/lib/straylight/tensors
Restart=on-failure
RestartSec=5
User=root
CapabilityBoundingSet=CAP_SYS_ADMIN
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

### Task 4: Tests for both subsystems

**Files:**
- `tests/unit/subsystems/test_enclave_attestation.cpp` (from Chunk 1 Task 1)
- `tests/unit/subsystems/test_enclave_inference.cpp`
- `tests/unit/subsystems/test_fuse_compression.cpp`
- `tests/unit/subsystems/test_fuse_cache.cpp`
- `tests/unit/subsystems/test_fuse_tensor_format.cpp`

- [ ] **Step 1: Create test_enclave_inference.cpp**

```cpp
// tests/unit/subsystems/test_enclave_inference.cpp
#include <gtest/gtest.h>
#include "secure_inference.h"
#include "sealed_storage.h"
#include <fstream>
#include <filesystem>

using namespace straylight::enclave;

class SecureInferenceTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_model_;
    void SetUp() override {
        tmp_model_ = std::filesystem::temp_directory_path() / "test_model.enc";
        // Create a sealed model file: [nonce:12][tag:16][ciphertext]
        SealedStore store;
        store.init(SgxMode::Stub);
        std::vector<uint8_t> model_data(1024, 0x42);
        auto sealed = store.seal(model_data, SealPolicy::MrEnclave);
        ASSERT_TRUE(sealed.has_value());
        std::ofstream f(tmp_model_, std::ios::binary);
        f.write(reinterpret_cast<const char*>(sealed->nonce.data()), 12);
        f.write(reinterpret_cast<const char*>(sealed->tag.data()), 16);
        f.write(reinterpret_cast<const char*>(sealed->ciphertext.data()),
                sealed->ciphertext.size());
    }
    void TearDown() override { std::filesystem::remove(tmp_model_); }
};

TEST_F(SecureInferenceTest, InferProducesEncryptedOutput) {
    SecureInference eng;
    auto cfg = Config::defaults();
    ASSERT_TRUE(eng.init(SgxMode::Stub, cfg).has_value());

    InferenceRequest req{.encrypted_model = tmp_model_,
                         .input_tensor = {1, 2, 3, 4}};
    auto result = eng.run(req);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->encrypted_output.empty());
    EXPECT_GT(result->latency_us, 0u);
}

TEST_F(SecureInferenceTest, BadModelPathFails) {
    SecureInference eng;
    eng.init(SgxMode::Stub, Config::defaults());
    InferenceRequest req{.encrypted_model = "/nonexistent", .input_tensor = {1}};
    EXPECT_FALSE(eng.run(req).has_value());
}
```

- [ ] **Step 2: Create test_fuse_compression.cpp**

```cpp
// tests/unit/subsystems/test_fuse_compression.cpp
#include <gtest/gtest.h>
#include "compression.h"

using namespace straylight::fuse;

TEST(TensorCompression, LZ4RoundTrip) {
    TensorCompressor c;
    std::vector<uint8_t> data(BLOCK_SIZE, 0);
    // Fill with pattern: mostly zeros (high sparsity)
    for (size_t i = 0; i < 100; ++i) data[i * 1000] = 0x42;

    auto compressed = c.compress(data, Codec::LZ4, DType::F32);
    ASSERT_TRUE(compressed.has_value());
    EXPECT_LT(compressed->first.size(), data.size());
    EXPECT_GT(compressed->second.sparsity, 0.9f);

    auto decompressed = c.decompress(compressed->first, Codec::LZ4, data.size());
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, data);
}

TEST(TensorCompression, ZstdRoundTrip) {
    TensorCompressor c;
    std::vector<uint8_t> data(BLOCK_SIZE);
    for (size_t i = 0; i < data.size(); ++i) data[i] = i % 256;

    auto compressed = c.compress(data, Codec::Zstd, DType::F32);
    ASSERT_TRUE(compressed.has_value());

    auto decompressed = c.decompress(compressed->first, Codec::Zstd, data.size());
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, data);
}

TEST(TensorCompression, EmptyBlockSucceeds) {
    TensorCompressor c;
    std::vector<uint8_t> empty;
    auto r = c.compress(empty, Codec::LZ4, DType::F32);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->second.original_bytes, 0u);
}
```

- [ ] **Step 3: Create test_fuse_cache.cpp**

```cpp
// tests/unit/subsystems/test_fuse_cache.cpp
#include <gtest/gtest.h>
#include "cache.h"

using namespace straylight::fuse;

TEST(BlockCache, PutGetHit) {
    BlockCache cache(1024 * 1024);
    CacheKey k{.inode = 1, .block_idx = 0};
    std::vector<uint8_t> data(256, 0xAB);
    cache.put(k, data);

    auto hit = cache.get(k);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(std::vector<uint8_t>(hit->begin(), hit->end()), data);
}

TEST(BlockCache, MissReturnsError) {
    BlockCache cache(1024);
    auto r = cache.get({.inode = 99, .block_idx = 0});
    EXPECT_FALSE(r.has_value());
}

TEST(BlockCache, EvictsLRU) {
    BlockCache cache(512); // 512 bytes max
    std::vector<uint8_t> d1(256, 0x01);
    std::vector<uint8_t> d2(256, 0x02);
    std::vector<uint8_t> d3(256, 0x03);

    cache.put({1, 0}, d1);
    cache.put({2, 0}, d2);
    EXPECT_EQ(cache.current_bytes(), 512u);

    cache.put({3, 0}, d3); // Evicts {1,0}
    EXPECT_FALSE(cache.get({1, 0}).has_value());
    EXPECT_TRUE(cache.get({2, 0}).has_value());
    EXPECT_TRUE(cache.get({3, 0}).has_value());
}

TEST(BlockCache, EvictByInode) {
    BlockCache cache(1024 * 1024);
    cache.put({5, 0}, std::vector<uint8_t>(100, 0));
    cache.put({5, 1}, std::vector<uint8_t>(100, 0));
    cache.put({6, 0}, std::vector<uint8_t>(100, 0));
    cache.evict(5);
    EXPECT_FALSE(cache.get({5, 0}).has_value());
    EXPECT_FALSE(cache.get({5, 1}).has_value());
    EXPECT_TRUE(cache.get({6, 0}).has_value());
}
```

- [ ] **Step 4: Create test_fuse_tensor_format.cpp**

```cpp
// tests/unit/subsystems/test_fuse_tensor_format.cpp
#include <gtest/gtest.h>
#include "tensor_format.h"

using namespace straylight::fuse;

TEST(TensorFormat, SerializeParseRoundTrip) {
    TensorHeader hdr{.dtype = DType::F32, .codec = Codec::Zstd,
                     .ndim = 2, .total_elements = 1024, .num_blocks = 4,
                     .shape = {32, 32}};
    std::vector<BlockIndex> blocks(4);
    for (uint32_t i = 0; i < 4; ++i) {
        blocks[i] = {.offset = i * 65536ULL, .compressed_size = 50000,
                     .original_size = 65536, .sparsity = 0.3f};
    }
    auto serialized = serialize_header(hdr, blocks);
    ASSERT_TRUE(serialized.has_value());

    auto parsed = parse_header(*serialized);
    ASSERT_TRUE(parsed.has_value());
    auto& [h2, b2] = *parsed;
    EXPECT_EQ(h2.magic, TENSOR_MAGIC);
    EXPECT_EQ(h2.dtype, DType::F32);
    EXPECT_EQ(h2.ndim, 2u);
    EXPECT_EQ(h2.shape, hdr.shape);
    EXPECT_EQ(b2.size(), 4u);
    EXPECT_EQ(b2[2].compressed_size, 50000u);
}

TEST(TensorFormat, BadMagicFails) {
    std::vector<uint8_t> bad = {0, 0, 0, 0};
    auto r = parse_header(bad);
    EXPECT_FALSE(r.has_value());
}
```

- [ ] **Step 5: Add tests CMakeLists.txt entries**

```cmake
# tests/unit/subsystems/CMakeLists.txt (append)
add_executable(test_enclave_attestation test_enclave_attestation.cpp
    ${PROJECT_SOURCE_DIR}/bin/enclave/attestation.cpp
    ${PROJECT_SOURCE_DIR}/bin/enclave/sealed_storage.cpp)
target_link_libraries(test_enclave_attestation PRIVATE GTest::gtest_main
    straylight-common straylight-hw OpenSSL::Crypto)

add_executable(test_enclave_inference test_enclave_inference.cpp
    ${PROJECT_SOURCE_DIR}/bin/enclave/secure_inference.cpp
    ${PROJECT_SOURCE_DIR}/bin/enclave/sealed_storage.cpp
    ${PROJECT_SOURCE_DIR}/bin/enclave/attestation.cpp)
target_link_libraries(test_enclave_inference PRIVATE GTest::gtest_main
    straylight-common straylight-ml straylight-hw OpenSSL::Crypto)

add_executable(test_fuse_compression test_fuse_compression.cpp
    ${PROJECT_SOURCE_DIR}/bin/fuse/compression.cpp
    ${PROJECT_SOURCE_DIR}/bin/fuse/tensor_format.cpp)
target_link_libraries(test_fuse_compression PRIVATE GTest::gtest_main
    straylight-common lz4 zstd)

add_executable(test_fuse_cache test_fuse_cache.cpp
    ${PROJECT_SOURCE_DIR}/bin/fuse/cache.cpp)
target_link_libraries(test_fuse_cache PRIVATE GTest::gtest_main straylight-common)

add_executable(test_fuse_tensor_format test_fuse_tensor_format.cpp
    ${PROJECT_SOURCE_DIR}/bin/fuse/tensor_format.cpp)
target_link_libraries(test_fuse_tensor_format PRIVATE GTest::gtest_main straylight-common)

gtest_discover_tests(test_enclave_attestation)
gtest_discover_tests(test_enclave_inference)
gtest_discover_tests(test_fuse_compression)
gtest_discover_tests(test_fuse_cache)
gtest_discover_tests(test_fuse_tensor_format)
```
