# Plan 7A: Exotic Subsystems — quantum, photonics, pmem

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 3 exotic on-demand tool binaries: `straylight-quantum` (state-vector gate simulator), `straylight-photonics` (photonic mesh simulation), and `straylight-pmem` (persistent memory allocator + checkpoints). After this plan, the `straylight-exotic` Debian package gains these three binaries.

**Architecture:** Three binaries under `bin/`. All are CLI tools with `main()` + arg parsing — run, do work, exit. `straylight-quantum` links `libstraylight-common` + Eigen. `straylight-photonics` links `libstraylight-common` + `libstraylight-hw`. `straylight-pmem` links `libstraylight-common` + `libstraylight-hw` + libpmem2.

**Tech Stack:** C++20, Eigen 3.4+, libpmem2 1.12+, nlohmann/json 3.11+, spdlog 1.13+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, libstraylight-hw)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Uses DAX `/dev/dax*`, `mmap`, Linux-specific APIs.

**Error handling rules:**
- Libraries (internal modules) return `Result<T, std::string>`
- On-demand tools translate to `Result<T, SLError>` only in `main()` for exit codes
- Use `Result<T,E>::ok(value)` / `Result<T,E>::error(err)` — never `std::unexpected`

---

## Chunk 1: straylight-quantum — State Vector Gate Simulator

`bin/quantum/` — On-demand tool that simulates quantum circuits using dense state-vector representation. Supports standard gate set (H, X, Y, Z, CNOT, Toffoli, parametric rotations), depolarizing/amplitude-damping noise, and measurement with Born-rule sampling. Links `libstraylight-common` + Eigen.

### File Structure

```
bin/quantum/
├── CMakeLists.txt
├── main.cpp
├── state_vector.h
├── state_vector.cpp
├── gates.h
├── gates.cpp
├── circuit.h
├── circuit.cpp
├── noise.h
├── noise.cpp
├── measure.h
└── measure.cpp
tests/unit/subsystems/
└── test_quantum_gates.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_quantum_gates.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_quantum_gates.cpp
#include <gtest/gtest.h>
#include "state_vector.h"
#include "gates.h"
#include "circuit.h"
#include "measure.h"
#include "noise.h"

using namespace straylight::quantum;

TEST(StateVector, InitToZero) {
    StateVector sv(2);  // 2 qubits → 4 amplitudes
    EXPECT_EQ(sv.num_qubits(), 2u);
    EXPECT_EQ(sv.dim(), 4u);
    // |00⟩ state: amplitude[0] = 1, rest = 0
    auto amp = sv.amplitude(0);
    EXPECT_NEAR(amp.real(), 1.0, 1e-12);
    EXPECT_NEAR(sv.amplitude(1).real(), 0.0, 1e-12);
}

TEST(Gates, HadamardCreatesSuper) {
    StateVector sv(1);
    auto H = gates::hadamard();
    sv.apply_single(H, 0);
    // |+⟩ = (|0⟩ + |1⟩)/√2
    EXPECT_NEAR(std::abs(sv.amplitude(0)), 1.0 / std::sqrt(2.0), 1e-10);
    EXPECT_NEAR(std::abs(sv.amplitude(1)), 1.0 / std::sqrt(2.0), 1e-10);
}

TEST(Gates, XFlipsBit) {
    StateVector sv(1);
    sv.apply_single(gates::pauli_x(), 0);
    EXPECT_NEAR(sv.amplitude(1).real(), 1.0, 1e-12);
    EXPECT_NEAR(sv.amplitude(0).real(), 0.0, 1e-12);
}

TEST(Gates, CNOTEntangles) {
    StateVector sv(2);
    sv.apply_single(gates::hadamard(), 0);
    sv.apply_two(gates::cnot(), 0, 1);
    // Bell state: (|00⟩ + |11⟩)/√2
    EXPECT_NEAR(std::abs(sv.amplitude(0b00)), 1.0 / std::sqrt(2.0), 1e-10);
    EXPECT_NEAR(std::abs(sv.amplitude(0b11)), 1.0 / std::sqrt(2.0), 1e-10);
    EXPECT_NEAR(std::abs(sv.amplitude(0b01)), 0.0, 1e-10);
    EXPECT_NEAR(std::abs(sv.amplitude(0b10)), 0.0, 1e-10);
}

TEST(Circuit, ApplySequence) {
    Circuit circ(2);
    circ.add_gate(GateOp::single(gates::hadamard(), 0));
    circ.add_gate(GateOp::two(gates::cnot(), 0, 1));
    auto sv = circ.execute();
    ASSERT_TRUE(sv.has_value());
    EXPECT_NEAR(sv.value().probability(0b00), 0.5, 1e-10);
}

TEST(Measure, CollapseToOutcome) {
    StateVector sv(1);
    sv.apply_single(gates::pauli_x(), 0);
    auto result = measure::measure_qubit(sv, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1u);  // Deterministic: |1⟩
}

TEST(Noise, DepolarizingPreservesNorm) {
    StateVector sv(1);
    sv.apply_single(gates::hadamard(), 0);
    noise::apply_depolarizing(sv, 0, 0.1);
    // Norm must still be 1.0 (trace-preserving)
    double norm = 0.0;
    for (size_t i = 0; i < sv.dim(); ++i)
        norm += std::norm(sv.amplitude(i));
    EXPECT_NEAR(norm, 1.0, 1e-8);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_quantum_gates test_quantum_gates.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/state_vector.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/gates.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/circuit.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/measure.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/noise.cpp)
target_include_directories(test_quantum_gates PRIVATE ${PROJECT_SOURCE_DIR}/bin/quantum)
target_link_libraries(test_quantum_gates PRIVATE straylight-common Eigen3::Eigen GTest::gtest_main)
gtest_discover_tests(test_quantum_gates)
```

Run: `cmake --build build --target test_quantum_gates && ctest --test-dir build -R test_quantum_gates` → expect 7 failures.

---

### Task 2: Implement state_vector

**Files:** `bin/quantum/state_vector.h`, `bin/quantum/state_vector.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/quantum/state_vector.h
#pragma once

#include <straylight/result.h>
#include <Eigen/Dense>
#include <complex>
#include <cstddef>

namespace straylight::quantum {

using Complex = std::complex<double>;
using Gate1 = Eigen::Matrix2cd;              // 2x2 single-qubit gate
using Gate2 = Eigen::Matrix4cd;              // 4x4 two-qubit gate
using Amplitudes = Eigen::VectorXcd;         // 2^n amplitude vector

class StateVector {
public:
    explicit StateVector(size_t num_qubits);

    [[nodiscard]] size_t num_qubits() const;
    [[nodiscard]] size_t dim() const;          // 2^n
    [[nodiscard]] Complex amplitude(size_t index) const;
    [[nodiscard]] double probability(size_t index) const;

    void apply_single(const Gate1& gate, size_t qubit);
    void apply_two(const Gate2& gate, size_t q0, size_t q1);
    void set_amplitude(size_t index, Complex val);
    void normalize();

private:
    size_t num_qubits_;
    Amplitudes amps_;
};

} // namespace straylight::quantum
```

- [ ] **Step 2: Implementation**

```cpp
// bin/quantum/state_vector.cpp
#include "state_vector.h"
#include <cassert>
#include <cmath>

namespace straylight::quantum {

StateVector::StateVector(size_t num_qubits)
    : num_qubits_(num_qubits), amps_(Amplitudes::Zero(1ULL << num_qubits))
{
    amps_(0) = 1.0;  // |00...0⟩
}

size_t StateVector::num_qubits() const { return num_qubits_; }
size_t StateVector::dim() const { return static_cast<size_t>(amps_.size()); }
Complex StateVector::amplitude(size_t i) const { return amps_(i); }
double StateVector::probability(size_t i) const { return std::norm(amps_(i)); }

void StateVector::set_amplitude(size_t i, Complex val) { amps_(i) = val; }

void StateVector::normalize() {
    double n = amps_.norm();
    if (n > 1e-15) amps_ /= n;
}

void StateVector::apply_single(const Gate1& gate, size_t qubit) {
    // Iterate pairs of amplitudes differing in bit `qubit`
    size_t n = dim();
    size_t bit = 1ULL << qubit;
    for (size_t i = 0; i < n; ++i) {
        if (i & bit) continue;  // process each pair once
        size_t j = i | bit;
        Complex a0 = amps_(i), a1 = amps_(j);
        amps_(i) = gate(0,0)*a0 + gate(0,1)*a1;
        amps_(j) = gate(1,0)*a0 + gate(1,1)*a1;
    }
}

void StateVector::apply_two(const Gate2& gate, size_t q0, size_t q1) {
    // Build full-space indices for each (b0,b1) combination
    size_t n = dim();
    size_t bit0 = 1ULL << q0, bit1 = 1ULL << q1;
    for (size_t i = 0; i < n; ++i) {
        if ((i & bit0) || (i & bit1)) continue;  // canonical: both bits 0
        size_t idx[4] = { i, i|bit0, i|bit1, i|bit0|bit1 };
        Eigen::Vector4cd v;
        for (int k = 0; k < 4; ++k) v(k) = amps_(idx[k]);
        Eigen::Vector4cd r = gate * v;
        for (int k = 0; k < 4; ++k) amps_(idx[k]) = r(k);
    }
}

} // namespace straylight::quantum
```

---

### Task 3: Implement gates

**Files:** `bin/quantum/gates.h`, `bin/quantum/gates.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/quantum/gates.h
#pragma once
#include "state_vector.h"

namespace straylight::quantum::gates {

Gate1 hadamard();               // H = (1/√2)[[1,1],[1,-1]]
Gate1 pauli_x();                // X = [[0,1],[1,0]]
Gate1 pauli_y();                // Y = [[0,-i],[i,0]]
Gate1 pauli_z();                // Z = [[1,0],[0,-1]]
Gate1 rx(double theta);         // Rx(θ) = cos(θ/2)I - i·sin(θ/2)X
Gate1 ry(double theta);         // Ry(θ) = cos(θ/2)I - i·sin(θ/2)Y
Gate1 rz(double theta);         // Rz(θ) = exp(-iθZ/2)
Gate2 cnot();                   // CNOT: control-target
Gate2 toffoli_partial();        // Used internally for Toffoli decomposition

} // namespace straylight::quantum::gates
```

- [ ] **Step 2: Implementation (core excerpts)**

```cpp
// bin/quantum/gates.cpp
#include "gates.h"
#include <cmath>

namespace straylight::quantum::gates {

Gate1 hadamard() {
    Gate1 H;
    double s = 1.0 / std::sqrt(2.0);
    H << s, s, s, -s;
    return H;
}

Gate1 pauli_x() { Gate1 X; X << 0,1, 1,0; return X; }
Gate1 pauli_y() { Gate1 Y; Y << 0,Complex(0,-1), Complex(0,1),0; return Y; }
Gate1 pauli_z() { Gate1 Z; Z << 1,0, 0,-1; return Z; }

Gate1 rx(double theta) {
    double c = std::cos(theta/2), s = std::sin(theta/2);
    Gate1 R; R << c, Complex(0,-s), Complex(0,-s), c; return R;
}
// ry, rz: same pattern with appropriate matrix elements
// ...

Gate2 cnot() {
    Gate2 C = Gate2::Zero();
    // |00⟩→|00⟩, |01⟩→|01⟩, |10⟩→|11⟩, |11⟩→|10⟩
    C(0,0) = 1; C(1,1) = 1; C(2,3) = 1; C(3,2) = 1;
    return C;
}

} // namespace straylight::quantum::gates
```

---

### Task 4: Implement circuit, noise, measure

**Files:** `bin/quantum/circuit.h`, `bin/quantum/circuit.cpp`, `bin/quantum/noise.h`, `bin/quantum/noise.cpp`, `bin/quantum/measure.h`, `bin/quantum/measure.cpp`

- [ ] **Step 1: Circuit header + impl**

```cpp
// bin/quantum/circuit.h
#pragma once
#include "state_vector.h"
#include <straylight/result.h>
#include <variant>
#include <vector>

namespace straylight::quantum {

struct GateOp {
    enum class Kind { Single, Two };
    Kind kind;
    size_t q0, q1;            // q1 unused for Single
    Gate1 g1;                 // populated for Single
    Gate2 g2;                 // populated for Two

    static GateOp single(Gate1 g, size_t q) { return {Kind::Single, q, 0, g, {}}; }
    static GateOp two(Gate2 g, size_t q0, size_t q1) { return {Kind::Two, q0, q1, {}, g}; }
};

class Circuit {
public:
    explicit Circuit(size_t num_qubits);
    void add_gate(GateOp op);
    [[nodiscard]] Result<StateVector, std::string> execute() const;

private:
    size_t num_qubits_;
    std::vector<GateOp> ops_;
};

} // namespace straylight::quantum
```

```cpp
// bin/quantum/circuit.cpp — execute applies each GateOp sequentially
// ...
Result<StateVector, std::string> Circuit::execute() const {
    StateVector sv(num_qubits_);
    for (const auto& op : ops_) {
        if (op.kind == GateOp::Kind::Single) sv.apply_single(op.g1, op.q0);
        else sv.apply_two(op.g2, op.q0, op.q1);
    }
    return Result<StateVector, std::string>::ok(std::move(sv));
}
```

- [ ] **Step 2: Noise**

```cpp
// bin/quantum/noise.h
#pragma once
#include "state_vector.h"

namespace straylight::quantum::noise {

// Depolarizing channel: with prob p, apply random Pauli {X,Y,Z}
void apply_depolarizing(StateVector& sv, size_t qubit, double p);

// Amplitude damping: T1 decay with probability gamma
void apply_amplitude_damping(StateVector& sv, size_t qubit, double gamma);

} // namespace straylight::quantum::noise
```

```cpp
// bin/quantum/noise.cpp — Kraus operator approach
// apply_depolarizing: with prob p/3 each, apply X/Y/Z via Kraus operators
// Renormalize after application to maintain trace = 1
// apply_amplitude_damping: K0 = [[1,0],[0,√(1-γ)]], K1 = [[0,√γ],[0,0]]
// Apply K0 and K1, sum resulting density contributions, renormalize
// ...
```

- [ ] **Step 3: Measure**

```cpp
// bin/quantum/measure.h
#pragma once
#include "state_vector.h"
#include <straylight/result.h>

namespace straylight::quantum::measure {

// Measure single qubit, collapse state, return 0 or 1
Result<size_t, std::string> measure_qubit(StateVector& sv, size_t qubit);

// Measure all qubits, return bitstring as integer
Result<size_t, std::string> measure_all(StateVector& sv);

// Sample n measurements without collapse (re-prepare each time)
Result<std::vector<size_t>, std::string> sample(const StateVector& sv, size_t n);

} // namespace straylight::quantum::measure
```

```cpp
// bin/quantum/measure.cpp
// measure_qubit: compute p0 = Σ|α_i|² where bit `qubit` of i is 0
// Draw uniform random, if < p0 → outcome 0, collapse amplitudes with bit=1 to 0
// Else outcome 1, collapse amplitudes with bit=0 to 0. Renormalize.
// ...
```

- [ ] **Step 4: main.cpp + CMakeLists.txt**

```cpp
// bin/quantum/main.cpp
#include "circuit.h"
#include "gates.h"
#include "measure.h"
#include "noise.h"
#include <spdlog/spdlog.h>
// CLI: --qubits N --circuit <json> --noise depolarizing:p --shots S
// Parse args, build Circuit from JSON gate list, optionally insert noise,
// execute and sample, print measurement histogram as JSON to stdout
// Return 0 on success, 1 on error via Result<T,SLError> translation
// ...
```

```cmake
# bin/quantum/CMakeLists.txt
add_executable(straylight-quantum
    main.cpp state_vector.cpp gates.cpp circuit.cpp noise.cpp measure.cpp)
target_link_libraries(straylight-quantum PRIVATE straylight-common Eigen3::Eigen spdlog::spdlog)
install(TARGETS straylight-quantum RUNTIME DESTINATION bin)
```

Run: `cmake --build build --target test_quantum_gates && ctest --test-dir build -R test_quantum_gates` → expect 7 passes.

---

## Chunk 2: straylight-photonics — Photonic Mesh Simulation

`bin/photonics/` — On-demand tool that simulates programmable photonic integrated circuits. Models Mach-Zehnder interferometers (MZIs), assembles them into Clements/Reck meshes implementing arbitrary unitary matrices, simulates photon detection with Poisson noise, and provides a hardware device I/O stub. Links `libstraylight-common` + `libstraylight-hw`.

### File Structure

```
bin/photonics/
├── CMakeLists.txt
├── main.cpp
├── mzi.h
├── mzi.cpp
├── mesh.h
├── mesh.cpp
├── detector.h
├── detector.cpp
├── device.h
└── device.cpp
tests/unit/subsystems/
└── test_photonics_mesh.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_photonics_mesh.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_photonics_mesh.cpp
#include <gtest/gtest.h>
#include "mzi.h"
#include "mesh.h"
#include "detector.h"

using namespace straylight::photonics;

TEST(MZI, IdentityAtZero) {
    // θ=0, φ=0 → identity (no mixing)
    auto U = MZI(0.0, 0.0).transfer_matrix();
    EXPECT_NEAR(std::abs(U(0,0)), 1.0, 1e-10);
    EXPECT_NEAR(std::abs(U(1,1)), 1.0, 1e-10);
    EXPECT_NEAR(std::abs(U(0,1)), 0.0, 1e-10);
}

TEST(MZI, BalancedSplitter) {
    // θ=π/2 → 50:50 beam splitter
    auto U = MZI(M_PI / 2.0, 0.0).transfer_matrix();
    EXPECT_NEAR(std::abs(U(0,0)), 1.0 / std::sqrt(2.0), 1e-10);
    EXPECT_NEAR(std::abs(U(0,1)), 1.0 / std::sqrt(2.0), 1e-10);
}

TEST(MZI, Unitarity) {
    MZI mzi(1.23, 0.45);
    auto U = mzi.transfer_matrix();
    auto prod = U.adjoint() * U;
    // Should be identity
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            EXPECT_NEAR(std::abs(prod(i,j) - (i==j ? 1.0 : 0.0)), 0.0, 1e-10);
}

TEST(Mesh, SquareMeshIsUnitary) {
    // 4-mode Clements mesh
    Mesh mesh(4);
    mesh.randomize_params(42);  // seed
    auto U = mesh.compute_unitary();
    EXPECT_EQ(U.rows(), 4);
    auto prod = U.adjoint() * U;
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(std::abs(prod(i,i)), 1.0, 1e-8);
}

TEST(Detector, ThresholdDetection) {
    // Input: [3.0, 0.0] photon counts → threshold detector
    Eigen::VectorXd counts(2);
    counts << 3.0, 0.0;
    auto result = Detector::threshold(counts);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()[0], 1u);  // detected
    EXPECT_EQ(result.value()[1], 0u);  // not detected
}

TEST(Detector, PNRWithNoise) {
    Eigen::VectorXd counts(2);
    counts << 5.0, 2.0;
    // Photon-number-resolving with Poisson noise
    auto result = Detector::pnr(counts, /*dark_count_rate=*/0.01, /*seed=*/42);
    ASSERT_TRUE(result.has_value());
    // Should be close to input (low noise)
    EXPECT_GE(result.value()[0], 3u);
    EXPECT_LE(result.value()[0], 8u);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_photonics_mesh test_photonics_mesh.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/mzi.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/mesh.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/detector.cpp)
target_include_directories(test_photonics_mesh PRIVATE ${PROJECT_SOURCE_DIR}/bin/photonics)
target_link_libraries(test_photonics_mesh PRIVATE straylight-common straylight-hw Eigen3::Eigen GTest::gtest_main)
gtest_discover_tests(test_photonics_mesh)
```

Run: `cmake --build build --target test_photonics_mesh && ctest --test-dir build -R test_photonics_mesh` → expect 6 failures.

---

### Task 2: Implement MZI

**Files:** `bin/photonics/mzi.h`, `bin/photonics/mzi.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/photonics/mzi.h
#pragma once
#include <Eigen/Dense>
#include <complex>

namespace straylight::photonics {

using Complex = std::complex<double>;
using Matrix2c = Eigen::Matrix2cd;

// Mach-Zehnder interferometer: two beam splitters with phase shifts
// U(θ,φ) = [[e^{iφ} cos(θ/2), -sin(θ/2)], [e^{iφ} sin(θ/2), cos(θ/2)]]
class MZI {
public:
    MZI(double theta, double phi);
    [[nodiscard]] Matrix2c transfer_matrix() const;
    void set_params(double theta, double phi);
    [[nodiscard]] double theta() const;
    [[nodiscard]] double phi() const;

private:
    double theta_, phi_;
};

} // namespace straylight::photonics
```

- [ ] **Step 2: Implementation**

```cpp
// bin/photonics/mzi.cpp
#include "mzi.h"
#include <cmath>

namespace straylight::photonics {

MZI::MZI(double theta, double phi) : theta_(theta), phi_(phi) {}

Matrix2c MZI::transfer_matrix() const {
    double c = std::cos(theta_ / 2.0), s = std::sin(theta_ / 2.0);
    Complex eiphi = std::exp(Complex(0, phi_));
    Matrix2c U;
    U << eiphi * c, -s,
         eiphi * s,  c;
    return U;
}

void MZI::set_params(double theta, double phi) { theta_ = theta; phi_ = phi; }
double MZI::theta() const { return theta_; }
double MZI::phi() const { return phi_; }

} // namespace straylight::photonics
```

---

### Task 3: Implement mesh

**Files:** `bin/photonics/mesh.h`, `bin/photonics/mesh.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/photonics/mesh.h
#pragma once
#include "mzi.h"
#include <Eigen/Dense>
#include <straylight/result.h>
#include <vector>

namespace straylight::photonics {

using MatrixXc = Eigen::MatrixXcd;

// Clements decomposition: triangular mesh of MZIs implementing NxN unitary
class Mesh {
public:
    explicit Mesh(size_t num_modes);
    void randomize_params(uint64_t seed);
    void set_params(const std::vector<std::pair<double,double>>& params);
    [[nodiscard]] MatrixXc compute_unitary() const;
    [[nodiscard]] size_t num_modes() const;
    [[nodiscard]] size_t num_mzis() const;

private:
    size_t num_modes_;
    // MZIs stored column-major: each column is a diagonal layer
    std::vector<MZI> mzis_;
    std::vector<std::pair<size_t,size_t>> topology_;  // (row, col) for each MZI
};

} // namespace straylight::photonics
```

- [ ] **Step 2: Implementation (core excerpt)**

```cpp
// bin/photonics/mesh.cpp
#include "mesh.h"
#include <random>

namespace straylight::photonics {

Mesh::Mesh(size_t num_modes) : num_modes_(num_modes) {
    // Clements layout: N(N-1)/2 MZIs in triangular arrangement
    for (size_t col = 0; col < num_modes_; ++col) {
        size_t start = col % 2;
        for (size_t row = start; row + 1 < num_modes_; row += 2) {
            mzis_.emplace_back(0.0, 0.0);
            topology_.emplace_back(row, col);
        }
    }
}

MatrixXc Mesh::compute_unitary() const {
    MatrixXc U = MatrixXc::Identity(num_modes_, num_modes_);
    for (size_t i = 0; i < mzis_.size(); ++i) {
        auto T = mzis_[i].transfer_matrix();
        size_t r = topology_[i].first;
        // Embed 2x2 into NxN at rows/cols (r, r+1) and multiply
        MatrixXc layer = MatrixXc::Identity(num_modes_, num_modes_);
        layer(r,r) = T(0,0); layer(r,r+1) = T(0,1);
        layer(r+1,r) = T(1,0); layer(r+1,r+1) = T(1,1);
        U = layer * U;
    }
    return U;
}

void Mesh::randomize_params(uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> dist_t(0, M_PI), dist_p(0, 2*M_PI);
    for (auto& mzi : mzis_) mzi.set_params(dist_t(rng), dist_p(rng));
}

// ...
} // namespace straylight::photonics
```

---

### Task 4: Implement detector, device, main

**Files:** `bin/photonics/detector.h`, `bin/photonics/detector.cpp`, `bin/photonics/device.h`, `bin/photonics/device.cpp`, `bin/photonics/main.cpp`, `bin/photonics/CMakeLists.txt`

- [ ] **Step 1: Detector**

```cpp
// bin/photonics/detector.h
#pragma once
#include <Eigen/Dense>
#include <straylight/result.h>
#include <vector>

namespace straylight::photonics {

class Detector {
public:
    // Threshold: any photons → 1, else 0
    static Result<std::vector<size_t>, std::string>
    threshold(const Eigen::VectorXd& photon_counts);

    // Photon-number-resolving with Poisson dark counts
    static Result<std::vector<size_t>, std::string>
    pnr(const Eigen::VectorXd& photon_counts, double dark_count_rate, uint64_t seed);
};

} // namespace straylight::photonics
```

```cpp
// bin/photonics/detector.cpp
// threshold: iterate counts, output 1 if count > 0.5, else 0
// pnr: for each mode, sample Poisson(count + dark_count_rate), return integer counts
// ...
```

- [ ] **Step 2: Device (hardware I/O stub)**

```cpp
// bin/photonics/device.h
#pragma once
#include <straylight/result.h>
#include <string>
#include <vector>

namespace straylight::photonics {

// Stub for USB/SPI communication with photonic hardware
class Device {
public:
    static Result<Device, std::string> open(const std::string& path);
    Result<void, std::string> write_params(const std::vector<std::pair<double,double>>& params);
    Result<std::vector<double>, std::string> read_detectors();
    void close();

private:
    int fd_ = -1;
    std::string path_;
};

} // namespace straylight::photonics
```

```cpp
// bin/photonics/device.cpp
// open: open(path, O_RDWR), return error if fd < 0
// write_params: serialize params as binary, write() to fd
// read_detectors: read() from fd, deserialize detector values
// close: ::close(fd_)
// All return Result-wrapped errors for missing hardware
// ...
```

- [ ] **Step 3: main.cpp + CMakeLists.txt**

```cpp
// bin/photonics/main.cpp
// CLI: --modes N --mesh clements|reck --params <json> --detect threshold|pnr
// Optional: --device /dev/ttyUSB0 for real hardware
// Build mesh, compute unitary, apply to input state, detect, print JSON output
// ...
```

```cmake
# bin/photonics/CMakeLists.txt
add_executable(straylight-photonics
    main.cpp mzi.cpp mesh.cpp detector.cpp device.cpp)
target_link_libraries(straylight-photonics PRIVATE
    straylight-common straylight-hw Eigen3::Eigen spdlog::spdlog)
install(TARGETS straylight-photonics RUNTIME DESTINATION bin)
```

Run: `cmake --build build --target test_photonics_mesh && ctest --test-dir build -R test_photonics_mesh` → expect 6 passes.

---

## Chunk 3: straylight-pmem — Persistent Memory Tools

`bin/pmem/` — On-demand tool providing persistent memory infrastructure: DAX device mapping, slab allocation over persistent regions, write-ahead logging for crash consistency, and tensor checkpoint save/restore. Links `libstraylight-common` + `libstraylight-hw` + libpmem2.

### File Structure

```
bin/pmem/
├── CMakeLists.txt
├── main.cpp
├── dax.h
├── dax.cpp
├── allocator.h
├── allocator.cpp
├── log.h
├── log.cpp
├── checkpoint.h
└── checkpoint.cpp
tests/unit/subsystems/
└── test_pmem_allocator.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_pmem_allocator.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_pmem_allocator.cpp
#include <gtest/gtest.h>
#include "dax.h"
#include "allocator.h"
#include "log.h"
#include "checkpoint.h"
#include <cstring>
#include <vector>

using namespace straylight::pmem;

// Tests use anonymous mmap (MAP_ANONYMOUS) to simulate DAX regions on any machine

TEST(DaxRegion, MapAnonymous) {
    auto region = DaxRegion::map_anonymous(4096);
    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region.value().size(), 4096u);
    EXPECT_NE(region.value().base(), nullptr);
}

TEST(SlabAllocator, AllocAndFree) {
    auto region = DaxRegion::map_anonymous(1 << 20);  // 1MB
    ASSERT_TRUE(region.has_value());
    SlabAllocator alloc(region.value());
    auto p1 = alloc.allocate(256);
    ASSERT_TRUE(p1.has_value());
    EXPECT_NE(p1.value(), nullptr);

    auto p2 = alloc.allocate(512);
    ASSERT_TRUE(p2.has_value());
    EXPECT_NE(p2.value(), p1.value());

    alloc.deallocate(p1.value());
    // Re-allocation should reuse freed slab
    auto p3 = alloc.allocate(256);
    ASSERT_TRUE(p3.has_value());
}

TEST(SlabAllocator, OOM) {
    auto region = DaxRegion::map_anonymous(1024);  // Tiny
    ASSERT_TRUE(region.has_value());
    SlabAllocator alloc(region.value());
    auto p = alloc.allocate(2048);  // Larger than region
    EXPECT_FALSE(p.has_value());
}

TEST(WriteAheadLog, AppendAndReplay) {
    auto region = DaxRegion::map_anonymous(1 << 16);  // 64KB
    ASSERT_TRUE(region.has_value());
    WriteAheadLog wal(region.value());

    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto seq = wal.append(data);
    ASSERT_TRUE(seq.has_value());
    EXPECT_GE(seq.value(), 1u);

    auto entries = wal.replay();
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries.value().size(), 1u);
    EXPECT_EQ(entries.value()[0].data, data);
}

TEST(Checkpoint, SaveAndRestore) {
    auto region = DaxRegion::map_anonymous(1 << 20);
    ASSERT_TRUE(region.has_value());
    SlabAllocator alloc(region.value());

    // Simulate a tensor: 4x4 float matrix
    std::vector<float> tensor(16, 3.14f);
    auto r = Checkpoint::save(alloc, "layer0.weight", tensor.data(), tensor.size() * sizeof(float));
    ASSERT_TRUE(r.has_value());

    auto restored = Checkpoint::restore(alloc, "layer0.weight");
    ASSERT_TRUE(restored.has_value());
    auto* fp = reinterpret_cast<const float*>(restored.value().data());
    EXPECT_NEAR(fp[0], 3.14f, 1e-5);
    EXPECT_NEAR(fp[15], 3.14f, 1e-5);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_pmem_allocator test_pmem_allocator.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/dax.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/allocator.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/log.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/checkpoint.cpp)
target_include_directories(test_pmem_allocator PRIVATE ${PROJECT_SOURCE_DIR}/bin/pmem)
target_link_libraries(test_pmem_allocator PRIVATE straylight-common straylight-hw GTest::gtest_main)
# Note: tests use MAP_ANONYMOUS, so libpmem2 not needed for unit tests
gtest_discover_tests(test_pmem_allocator)
```

Run: `cmake --build build --target test_pmem_allocator && ctest --test-dir build -R test_pmem_allocator` → expect 5 failures.

---

### Task 2: Implement dax

**Files:** `bin/pmem/dax.h`, `bin/pmem/dax.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/pmem/dax.h
#pragma once
#include <straylight/result.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace straylight::pmem {

class DaxRegion {
public:
    // Map a real DAX device: /dev/dax0.0, etc.
    static Result<DaxRegion, std::string> map_device(const std::string& path);
    // Map anonymous memory (for testing without real pmem hardware)
    static Result<DaxRegion, std::string> map_anonymous(size_t size);

    [[nodiscard]] uint8_t* base() const;
    [[nodiscard]] size_t size() const;
    void persist(size_t offset, size_t len);  // clflush/clwb + sfence
    ~DaxRegion();

    DaxRegion(DaxRegion&& o) noexcept;
    DaxRegion& operator=(DaxRegion&& o) noexcept;
    DaxRegion(const DaxRegion&) = delete;

private:
    DaxRegion(uint8_t* base, size_t size, bool is_device);
    uint8_t* base_ = nullptr;
    size_t size_ = 0;
    bool is_device_ = false;
};

} // namespace straylight::pmem
```

- [ ] **Step 2: Implementation**

```cpp
// bin/pmem/dax.cpp
#include "dax.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace straylight::pmem {

DaxRegion::DaxRegion(uint8_t* base, size_t size, bool is_device)
    : base_(base), size_(size), is_device_(is_device) {}

DaxRegion::~DaxRegion() {
    if (base_) munmap(base_, size_);
}
// Move ctor/assign: swap base_/size_/is_device_, null out source

Result<DaxRegion, std::string> DaxRegion::map_anonymous(size_t size) {
    void* p = mmap(nullptr, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return Result<DaxRegion, std::string>::error("mmap failed");
    return Result<DaxRegion, std::string>::ok(
        DaxRegion(static_cast<uint8_t*>(p), size, false));
}

Result<DaxRegion, std::string> DaxRegion::map_device(const std::string& path) {
    int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) return Result<DaxRegion, std::string>::error("open: " + path);
    // fstat to get size, mmap with MAP_SHARED|MAP_SYNC for DAX
    // close(fd) after mmap
    // ...
    return Result<DaxRegion, std::string>::error("stub: implement with libpmem2");
}

void DaxRegion::persist(size_t offset, size_t len) {
    if (!is_device_) return;  // no-op for anonymous
    // clflushopt each cache line in [base_+offset, base_+offset+len), then sfence
    // ...
}

uint8_t* DaxRegion::base() const { return base_; }
size_t DaxRegion::size() const { return size_; }

} // namespace straylight::pmem
```

---

### Task 3: Implement allocator + WAL

**Files:** `bin/pmem/allocator.h`, `bin/pmem/allocator.cpp`, `bin/pmem/log.h`, `bin/pmem/log.cpp`

- [ ] **Step 1: Slab allocator**

```cpp
// bin/pmem/allocator.h
#pragma once
#include "dax.h"
#include <straylight/result.h>
#include <map>

namespace straylight::pmem {

// Persistent slab allocator: fixed-size slabs (64B, 256B, 1KB, 4KB, 64KB)
// Free lists stored in-region header for crash recovery
class SlabAllocator {
public:
    explicit SlabAllocator(DaxRegion& region);
    Result<void*, std::string> allocate(size_t size);
    void deallocate(void* ptr);
    [[nodiscard]] size_t free_bytes() const;

private:
    DaxRegion& region_;
    // Header at base: magic, slab class free-list heads, watermark
    struct Header { uint64_t magic; size_t watermark; /* free list heads */ };
    Header* header_;
    // Round size up to nearest slab class, pop from free list or bump watermark
};

} // namespace straylight::pmem
```

```cpp
// bin/pmem/allocator.cpp
// allocate: pick slab class >= size, check free list, else bump watermark
// deallocate: push onto slab class free list, persist header
// Constructor: if magic present, recover from existing header; else initialize
// ...
```

- [ ] **Step 2: Write-ahead log**

```cpp
// bin/pmem/log.h
#pragma once
#include "dax.h"
#include <straylight/result.h>
#include <vector>
#include <cstdint>

namespace straylight::pmem {

struct LogEntry {
    uint64_t seq;
    std::vector<uint8_t> data;
};

// Append-only WAL in persistent region for crash consistency
class WriteAheadLog {
public:
    explicit WriteAheadLog(DaxRegion& region);
    Result<uint64_t, std::string> append(const std::vector<uint8_t>& data);
    Result<std::vector<LogEntry>, std::string> replay() const;
    void truncate_before(uint64_t seq);

private:
    DaxRegion& region_;
    // On-region layout: [header: tail_offset, seq_counter] [entry: len, seq, crc32, data]*
    struct WalHeader { uint64_t tail_offset; uint64_t next_seq; };
    WalHeader* header_;
};

} // namespace straylight::pmem
```

```cpp
// bin/pmem/log.cpp
// append: write entry at tail_offset, persist, bump tail_offset + next_seq, persist header
// replay: scan from first entry to tail_offset, verify CRC32 of each, collect valid
// truncate_before: advance head pointer, allowing space reuse
// ...
```

---

### Task 4: Implement checkpoint, main

**Files:** `bin/pmem/checkpoint.h`, `bin/pmem/checkpoint.cpp`, `bin/pmem/main.cpp`, `bin/pmem/CMakeLists.txt`

- [ ] **Step 1: Checkpoint**

```cpp
// bin/pmem/checkpoint.h
#pragma once
#include "allocator.h"
#include <straylight/result.h>
#include <span>
#include <string>

namespace straylight::pmem {

// Save/restore named tensor blobs to persistent memory
class Checkpoint {
public:
    static Result<void, std::string>
    save(SlabAllocator& alloc, const std::string& name, const void* data, size_t bytes);

    static Result<std::span<const uint8_t>, std::string>
    restore(SlabAllocator& alloc, const std::string& name);

    static Result<std::vector<std::string>, std::string>
    list(SlabAllocator& alloc);
};

} // namespace straylight::pmem
```

```cpp
// bin/pmem/checkpoint.cpp
// save: allocate slab for header (name + size) + slab(s) for data, memcpy, persist
// restore: look up name in checkpoint index, return span over persistent region
// list: iterate checkpoint index, collect names
// Index is a small hash map stored in a reserved region at known offset
// ...
```

- [ ] **Step 2: main.cpp + CMakeLists.txt**

```cpp
// bin/pmem/main.cpp
// CLI: --device /dev/dax0.0 | --anonymous SIZE
// Subcommands: alloc SIZE, free ADDR, wal-append DATA, wal-replay,
//              checkpoint-save NAME FILE, checkpoint-restore NAME, checkpoint-list
// Parse args, open region, dispatch subcommand, print JSON result
// ...
```

```cmake
# bin/pmem/CMakeLists.txt
add_executable(straylight-pmem
    main.cpp dax.cpp allocator.cpp log.cpp checkpoint.cpp)
target_link_libraries(straylight-pmem PRIVATE
    straylight-common straylight-hw spdlog::spdlog)
# Link libpmem2 only when available (optional; tests use MAP_ANONYMOUS)
find_library(PMEM2_LIB pmem2)
if(PMEM2_LIB)
    target_link_libraries(straylight-pmem PRIVATE ${PMEM2_LIB})
    target_compile_definitions(straylight-pmem PRIVATE HAS_LIBPMEM2)
endif()
install(TARGETS straylight-pmem RUNTIME DESTINATION bin)
```

Run: `cmake --build build --target test_pmem_allocator && ctest --test-dir build -R test_pmem_allocator` → expect 5 passes.

---

## Chunk 4: Integration, Packaging, Validation

Wire the three exotic binaries into the build system, Debian packaging, and cross-subsystem integration tests.

### Task 1: Top-level CMake integration

- [ ] **Step 1: Add subdirectories to top-level `CMakeLists.txt`**

```cmake
# In top-level CMakeLists.txt, add under the bin/ section:
add_subdirectory(bin/quantum)
add_subdirectory(bin/photonics)
add_subdirectory(bin/pmem)
```

- [ ] **Step 2: Verify all three build**

```bash
cmake --build build --target straylight-quantum straylight-photonics straylight-pmem
```

---

### Task 2: Debian packaging

- [ ] **Step 1: Update `debian/straylight-exotic.install`**

```
usr/bin/straylight-quantum
usr/bin/straylight-photonics
usr/bin/straylight-pmem
```

- [ ] **Step 2: Update `debian/control` — straylight-exotic stanza**

```
Package: straylight-exotic
Architecture: amd64
Depends: straylight-common (= ${binary:Version}), libeigen3-dev (>= 3.4), ${shlibs:Depends}
Recommends: libpmem2-1 (>= 1.12)
Description: StrayLight OS exotic subsystem tools
 Quantum gate simulator, photonic mesh simulator, and persistent memory tools.
```

---

### Task 3: udev rule for pmem

- [ ] **Step 1: Create `etc/udev/rules.d/90-straylight-pmem.rules`**

```
# Grant straylight group access to DAX devices for persistent memory tools
SUBSYSTEM=="dax", MODE="0660", GROUP="straylight"
```

---

### Task 4: Integration test

- [ ] **Step 1: Write cross-subsystem integration test**

**File:** `tests/integration/test_exotic_pipeline.cpp`

```cpp
// tests/integration/test_exotic_pipeline.cpp
#include <gtest/gtest.h>
#include "state_vector.h"
#include "gates.h"
#include "circuit.h"
#include "measure.h"
#include "mzi.h"
#include "mesh.h"
#include "detector.h"
#include "dax.h"
#include "allocator.h"
#include "checkpoint.h"

// End-to-end: quantum circuit → checkpoint state to pmem → restore and verify
TEST(ExoticPipeline, QuantumCheckpointRoundTrip) {
    using namespace straylight::quantum;
    // Build and execute a Bell circuit
    Circuit circ(2);
    circ.add_gate(GateOp::single(gates::hadamard(), 0));
    circ.add_gate(GateOp::two(gates::cnot(), 0, 1));
    auto sv = circ.execute();
    ASSERT_TRUE(sv.has_value());

    // Checkpoint the state vector amplitudes to pmem
    using namespace straylight::pmem;
    auto region = DaxRegion::map_anonymous(1 << 20);
    ASSERT_TRUE(region.has_value());
    SlabAllocator alloc(region.value());

    size_t bytes = sv.value().dim() * sizeof(std::complex<double>);
    auto r = Checkpoint::save(alloc, "bell_state", &sv.value().amplitude(0), bytes);
    ASSERT_TRUE(r.has_value());

    // Restore and verify
    auto restored = Checkpoint::restore(alloc, "bell_state");
    ASSERT_TRUE(restored.has_value());
    auto* amps = reinterpret_cast<const std::complex<double>*>(restored.value().data());
    EXPECT_NEAR(std::abs(amps[0]), 1.0 / std::sqrt(2.0), 1e-10);
    EXPECT_NEAR(std::abs(amps[3]), 1.0 / std::sqrt(2.0), 1e-10);
}

// Photonic mesh → detect → verify unitarity preserved
TEST(ExoticPipeline, PhotonicDetectionChain) {
    using namespace straylight::photonics;
    Mesh mesh(4);
    mesh.randomize_params(123);
    auto U = mesh.compute_unitary();
    // Input: single photon in mode 0
    Eigen::VectorXcd input = Eigen::VectorXcd::Zero(4);
    input(0) = 1.0;
    Eigen::VectorXcd output = U * input;
    // Total probability conserved
    double total = output.squaredNorm();
    EXPECT_NEAR(total, 1.0, 1e-10);
    // Detect
    Eigen::VectorXd counts(4);
    for (int i = 0; i < 4; ++i) counts(i) = std::norm(output(i));
    auto det = Detector::threshold(counts);
    ASSERT_TRUE(det.has_value());
}
```

- [ ] **Step 2: Add to `tests/integration/CMakeLists.txt`**

```cmake
add_executable(test_exotic_pipeline test_exotic_pipeline.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/state_vector.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/gates.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/circuit.cpp
    ${PROJECT_SOURCE_DIR}/bin/quantum/measure.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/mzi.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/mesh.cpp
    ${PROJECT_SOURCE_DIR}/bin/photonics/detector.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/dax.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/allocator.cpp
    ${PROJECT_SOURCE_DIR}/bin/pmem/checkpoint.cpp)
target_include_directories(test_exotic_pipeline PRIVATE
    ${PROJECT_SOURCE_DIR}/bin/quantum
    ${PROJECT_SOURCE_DIR}/bin/photonics
    ${PROJECT_SOURCE_DIR}/bin/pmem)
target_link_libraries(test_exotic_pipeline PRIVATE
    straylight-common straylight-hw Eigen3::Eigen GTest::gtest_main)
gtest_discover_tests(test_exotic_pipeline)
```

---

### Validation checklist

After all 4 chunks are complete:

```bash
# Build everything
cmake --build build --target straylight-quantum straylight-photonics straylight-pmem

# Run unit tests
ctest --test-dir build -R "test_quantum_gates|test_photonics_mesh|test_pmem_allocator"

# Run integration test
ctest --test-dir build -R test_exotic_pipeline

# Verify binaries exist
ls -la build/bin/quantum/straylight-quantum
ls -la build/bin/photonics/straylight-photonics
ls -la build/bin/pmem/straylight-pmem

# Verify Debian packaging
dpkg-buildpackage -b -uc -us
dpkg-deb -c ../straylight-exotic_*.deb | grep -E "quantum|photonics|pmem"
```

Expected: 18 unit tests pass (7 quantum + 6 photonics + 5 pmem), 2 integration tests pass, 3 binaries installed.
