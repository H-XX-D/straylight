# Plan 1: Foundation — Build System & Shared Libraries

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the monorepo build system, all 4 shared libraries (libstraylight-common, libstraylight-ml, libstraylight-net, libstraylight-hw), and the test infrastructure so that all subsequent plans have a working foundation to link against.

**Architecture:** Layered monorepo with CMake 3.25+. Four shared libraries provide common abstractions (logging, IPC, config, error handling, tensor types, network sockets, hardware access). Every subsequent binary links at least libstraylight-common. Libraries use SO versioning with hidden-by-default symbol visibility.

**Tech Stack:** C++20, CMake 3.25+, spdlog 1.13+, nlohmann/json 3.11+, sdbus-c++ 2.0+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

---

## Chunk 1: Build System Skeleton

### File Structure

```
straylight/
├── CMakeLists.txt                        # Root build definition
├── CMakePresets.json                     # Dev, release, package, test presets
├── cmake/
│   ├── StraylightCommon.cmake            # Shared compiler flags, sanitizers, STRAYLIGHT_EXPORT macro
│   └── StraylightVersion.cmake           # Version variables
├── lib/
│   ├── common/
│   │   ├── CMakeLists.txt
│   │   ├── include/straylight/common.h   # Umbrella header
│   │   ├── include/straylight/version.h  # Version macros
│   │   └── include/straylight/export.h   # STRAYLIGHT_EXPORT definition
│   ├── ml/
│   │   └── CMakeLists.txt
│   ├── net/
│   │   └── CMakeLists.txt
│   └── hw/
│       └── CMakeLists.txt
├── tests/
│   ├── CMakeLists.txt
│   └── unit/
│       └── common/
│           └── CMakeLists.txt
└── .clang-format                         # Project code style
```

### Task 1: Root CMakeLists.txt and Presets

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/StraylightCommon.cmake`
- Create: `cmake/StraylightVersion.cmake`

- [ ] **Step 1: Create root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.25)
project(straylight VERSION 1.0.0 LANGUAGES CXX C)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include shared cmake modules
include(cmake/StraylightVersion.cmake)
include(cmake/StraylightCommon.cmake)

# Build options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_COMPOSITOR "Build Wayland compositor" ON)
option(BUILD_SHELL "Build desktop shell" ON)
option(BUILD_APPS "Build built-in applications" ON)
option(BUILD_SUBSYSTEMS "Build subsystem binaries" ON)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Shared libraries
add_subdirectory(lib/common)
add_subdirectory(lib/ml)
add_subdirectory(lib/net)
add_subdirectory(lib/hw)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Status
message(STATUS "========================================")
message(STATUS "StrayLight OS v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "  C++ Standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:         ${BUILD_TESTS}")
message(STATUS "  Compositor:    ${BUILD_COMPOSITOR}")
message(STATUS "  Shell:         ${BUILD_SHELL}")
message(STATUS "  Apps:          ${BUILD_APPS}")
message(STATUS "  Subsystems:    ${BUILD_SUBSYSTEMS}")
message(STATUS "========================================")
```

- [ ] **Step 2: Create cmake/StraylightVersion.cmake**

```cmake
# Shared version variables derived from project()
set(STRAYLIGHT_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(STRAYLIGHT_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(STRAYLIGHT_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(STRAYLIGHT_VERSION "${PROJECT_VERSION}")

# SO version: major version for ABI compatibility
set(STRAYLIGHT_SO_VERSION ${PROJECT_VERSION_MAJOR})
```

- [ ] **Step 3: Create cmake/StraylightCommon.cmake**

```cmake
# Shared compiler flags for all StrayLight targets

# Strict warnings
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Werror=return-type
    -Werror=uninitialized
)

# Symbol visibility: hidden by default
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Position-independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sanitizers in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(ENABLE_SANITIZERS "Enable ASan + UBSan" ON)
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# LTO in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# Helper function to create a StrayLight shared library
function(straylight_add_library target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_HEADERS;DEPS" ${ARGN})

    add_library(${target_name} SHARED ${ARG_SOURCES})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    set_target_properties(${target_name} PROPERTIES
        VERSION ${STRAYLIGHT_VERSION}
        SOVERSION ${STRAYLIGHT_SO_VERSION}
        EXPORT_NAME ${target_name}
    )

    if(ARG_DEPS)
        target_link_libraries(${target_name} PUBLIC ${ARG_DEPS})
    endif()
endfunction()
```

- [ ] **Step 4: Create CMakePresets.json**

```json
{
    "version": 6,
    "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
    "configurePresets": [
        {
            "name": "dev",
            "displayName": "Development",
            "binaryDir": "${sourceDir}/build/dev",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "BUILD_TESTS": "ON",
                "ENABLE_SANITIZERS": "ON"
            }
        },
        {
            "name": "release",
            "displayName": "Release",
            "binaryDir": "${sourceDir}/build/release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "BUILD_TESTS": "OFF"
            }
        },
        {
            "name": "test",
            "displayName": "Test with Coverage",
            "binaryDir": "${sourceDir}/build/test",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "BUILD_TESTS": "ON",
                "CMAKE_CXX_FLAGS": "--coverage"
            }
        },
        {
            "name": "package",
            "displayName": "Packaging",
            "binaryDir": "${sourceDir}/build/package",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "RelWithDebInfo",
                "BUILD_TESTS": "OFF"
            }
        }
    ],
    "buildPresets": [
        { "name": "dev", "configurePreset": "dev" },
        { "name": "release", "configurePreset": "release" },
        { "name": "test", "configurePreset": "test" },
        { "name": "package", "configurePreset": "package" }
    ],
    "testPresets": [
        {
            "name": "dev",
            "configurePreset": "dev",
            "output": { "outputOnFailure": true }
        }
    ]
}
```

- [ ] **Step 5: Create .clang-format**

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
BreakBeforeBraces: Attach
NamespaceIndentation: None
SortIncludes: CaseInsensitive
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^<straylight/'
    Priority: 1
  - Regex: '^<'
    Priority: 2
  - Regex: '.*'
    Priority: 3
```

- [ ] **Step 6: Commit build system skeleton**

```bash
git add CMakeLists.txt CMakePresets.json cmake/ .clang-format
git commit -m "build: root CMake build system with presets and shared config"
```

---

### Task 2: Export Header and Version Header

**Files:**
- Create: `lib/common/include/straylight/export.h`
- Create: `lib/common/include/straylight/version.h`

- [ ] **Step 1: Create export.h**

```cpp
#pragma once

// Symbol visibility macros for shared libraries.
// All symbols are hidden by default (CMAKE_CXX_VISIBILITY_PRESET=hidden).
// Use STRAYLIGHT_EXPORT on public API functions/classes.

#if defined(_WIN32)
    #define STRAYLIGHT_EXPORT __declspec(dllexport)
    #define STRAYLIGHT_IMPORT __declspec(dllimport)
#else
    #define STRAYLIGHT_EXPORT __attribute__((visibility("default")))
    #define STRAYLIGHT_IMPORT __attribute__((visibility("default")))
#endif

// Each library defines its own API macro. Example:
// #ifdef straylight_common_EXPORTS
// #define STRAYLIGHT_COMMON_API STRAYLIGHT_EXPORT
// #else
// #define STRAYLIGHT_COMMON_API STRAYLIGHT_IMPORT
// #endif
```

- [ ] **Step 2: Create version.h**

```cpp
#pragma once

#define STRAYLIGHT_VERSION_MAJOR 1
#define STRAYLIGHT_VERSION_MINOR 0
#define STRAYLIGHT_VERSION_PATCH 0
#define STRAYLIGHT_VERSION "1.0.0"

namespace straylight {

struct Version {
    static constexpr int major = STRAYLIGHT_VERSION_MAJOR;
    static constexpr int minor = STRAYLIGHT_VERSION_MINOR;
    static constexpr int patch = STRAYLIGHT_VERSION_PATCH;
    static constexpr const char* string = STRAYLIGHT_VERSION;
};

} // namespace straylight
```

- [ ] **Step 3: Commit**

```bash
git add lib/common/include/straylight/export.h lib/common/include/straylight/version.h
git commit -m "feat: add symbol visibility export macros and version header"
```

---

### Task 3: libstraylight-common — Result Type

**Files:**
- Create: `lib/common/include/straylight/result.h`
- Create: `tests/unit/common/test_result.cpp`
- Create: `tests/unit/common/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Create tests/CMakeLists.txt**

```cmake
find_package(GTest 1.14 REQUIRED)
include(GoogleTest)

add_subdirectory(unit/common)
```

- [ ] **Step 2: Create tests/unit/common/CMakeLists.txt**

```cmake
add_executable(test_common_result test_result.cpp)
target_link_libraries(test_common_result PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_result)
```

- [ ] **Step 3: Write failing test for Result type**

```cpp
// tests/unit/common/test_result.cpp
#include <gtest/gtest.h>
#include <straylight/result.h>
#include <string>

using namespace straylight;

TEST(ResultTest, OkValueCanBeAccessed) {
    auto r = Result<int, std::string>::ok(42);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrorCanBeAccessed) {
    auto r = Result<int, std::string>::error("something failed");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), "something failed");
}

TEST(ResultTest, MapTransformsValue) {
    auto r = Result<int, std::string>::ok(10);
    auto mapped = r.map([](int v) { return v * 2; });
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), 20);
}

TEST(ResultTest, MapPassesThroughError) {
    auto r = Result<int, std::string>::error("fail");
    auto mapped = r.map([](int v) { return v * 2; });
    ASSERT_FALSE(mapped.has_value());
    EXPECT_EQ(mapped.error(), "fail");
}

TEST(ResultTest, AndThenChainsOperations) {
    auto r = Result<int, std::string>::ok(10);
    auto chained = r.and_then([](int v) -> Result<std::string, std::string> {
        return Result<std::string, std::string>::ok(std::to_string(v));
    });
    ASSERT_TRUE(chained.has_value());
    EXPECT_EQ(chained.value(), "10");
}

TEST(ResultTest, ValueOrReturnsDefaultOnError) {
    auto r = Result<int, std::string>::error("fail");
    EXPECT_EQ(r.value_or(99), 99);
}
```

- [ ] **Step 4: Run test to verify it fails**

```bash
cmake --preset dev && cmake --build build/dev --target test_common_result
# Expected: FAIL - straylight/result.h not found
```

- [ ] **Step 5: Implement Result type**

```cpp
// lib/common/include/straylight/result.h
#pragma once

#include <functional>
#include <stdexcept>
#include <variant>

namespace straylight {

/// Result<T, E> — a value-or-error type inspired by Rust's Result and C++23 std::expected.
/// Use this instead of exceptions for recoverable errors.
template <typename T, typename E>
class Result {
public:
    /// Create a successful result.
    static Result ok(T value) { return Result(std::move(value)); }

    /// Create an error result.
    static Result error(E err) { return Result(err_tag{}, std::move(err)); }

    /// Returns true if this result contains a value (not an error).
    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    /// Returns the contained value. Throws if this is an error.
    [[nodiscard]] const T& value() const& {
        if (!has_value()) {
            throw std::logic_error("Result::value() called on error");
        }
        return std::get<T>(storage_);
    }

    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            throw std::logic_error("Result::value() called on error");
        }
        return std::get<T>(std::move(storage_));
    }

    /// Returns the contained error. Throws if this is a value.
    [[nodiscard]] const E& error() const& {
        if (has_value()) {
            throw std::logic_error("Result::error() called on value");
        }
        return std::get<err_wrapper>(storage_).err;
    }

    /// Returns value if ok, or the provided default if error.
    [[nodiscard]] T value_or(T default_val) const& {
        return has_value() ? std::get<T>(storage_) : std::move(default_val);
    }

    /// Transform the value with f, pass through errors unchanged.
    template <typename F>
    auto map(F&& f) const -> Result<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (has_value()) {
            return Result<U, E>::ok(f(std::get<T>(storage_)));
        }
        return Result<U, E>::error(std::get<err_wrapper>(storage_).err);
    }

    /// Chain operations that return Result.
    template <typename F>
    auto and_then(F&& f) const -> decltype(f(std::declval<T>())) {
        using R = decltype(f(std::declval<T>()));
        if (has_value()) {
            return f(std::get<T>(storage_));
        }
        return R::error(std::get<err_wrapper>(storage_).err);
    }

private:
    struct err_tag {};
    struct err_wrapper { E err; };

    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(err_tag, E err) : storage_(err_wrapper{std::move(err)}) {}

    std::variant<T, err_wrapper> storage_;
};

} // namespace straylight
```

- [ ] **Step 6: Create lib/common/CMakeLists.txt (header-only for now, will add sources)**

```cmake
add_library(straylight-common SHARED
    src/stub.cpp   # Placeholder until we add real sources
)

target_include_directories(straylight-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(straylight-common PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

# API export macro
target_compile_definitions(straylight-common PRIVATE straylight_common_EXPORTS)
```

- [ ] **Step 7: Create stub source**

```cpp
// lib/common/src/stub.cpp
// Placeholder to produce a .so — will be replaced by real sources.
namespace straylight { namespace detail { void common_stub() {} } }
```

- [ ] **Step 8: Create placeholder CMakeLists for other libs**

```cmake
# lib/ml/CMakeLists.txt
add_library(straylight-ml SHARED src/stub.cpp)
target_include_directories(straylight-ml PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(straylight-ml PUBLIC straylight-common)
set_target_properties(straylight-ml PROPERTIES VERSION ${STRAYLIGHT_VERSION} SOVERSION ${STRAYLIGHT_SO_VERSION})
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/src/stub.cpp "namespace straylight::ml { void stub() {} }\n")
```

```cmake
# lib/net/CMakeLists.txt
add_library(straylight-net SHARED src/stub.cpp)
target_include_directories(straylight-net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(straylight-net PUBLIC straylight-common)
set_target_properties(straylight-net PROPERTIES VERSION ${STRAYLIGHT_VERSION} SOVERSION ${STRAYLIGHT_SO_VERSION})
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/src/stub.cpp "namespace straylight::net { void stub() {} }\n")
```

```cmake
# lib/hw/CMakeLists.txt
add_library(straylight-hw SHARED src/stub.cpp)
target_include_directories(straylight-hw PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(straylight-hw PUBLIC straylight-common)
set_target_properties(straylight-hw PROPERTIES VERSION ${STRAYLIGHT_VERSION} SOVERSION ${STRAYLIGHT_SO_VERSION})
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/src/stub.cpp "namespace straylight::hw { void stub() {} }\n")
```

- [ ] **Step 9: Build and run tests**

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
# Expected: All 6 Result tests pass
```

- [ ] **Step 10: Commit**

```bash
git add lib/ tests/
git commit -m "feat(common): add Result<T,E> type with map/and_then/value_or"
```

---

### Task 4: libstraylight-common — Logging (spdlog wrapper)

**Files:**
- Create: `lib/common/include/straylight/log.h`
- Create: `lib/common/src/log.cpp`
- Create: `tests/unit/common/test_log.cpp`
- Modify: `lib/common/CMakeLists.txt`
- Modify: `tests/unit/common/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

```cpp
// tests/unit/common/test_log.cpp
#include <gtest/gtest.h>
#include <straylight/log.h>

using namespace straylight;

TEST(LogTest, InitializeCreatesLogger) {
    Log::init("test-app", Log::Level::Debug);
    // Should not throw
    auto logger = Log::get();
    ASSERT_NE(logger, nullptr);
}

TEST(LogTest, LogMacrosDoNotCrash) {
    Log::init("test-macros", Log::Level::Trace);
    // These should all execute without crashing
    SL_TRACE("trace message: {}", 1);
    SL_DEBUG("debug message: {}", 2);
    SL_INFO("info message: {}", 3);
    SL_WARN("warning message: {}", 4);
    SL_ERROR("error message: {}", 5);
}

TEST(LogTest, SubsystemLoggerHasPrefix) {
    Log::init("test-app", Log::Level::Debug);
    auto sub = Log::subsystem("entropy");
    ASSERT_NE(sub, nullptr);
}
```

- [ ] **Step 2: Add test to CMakeLists**

Add to `tests/unit/common/CMakeLists.txt`:
```cmake
add_executable(test_common_log test_log.cpp)
target_link_libraries(test_common_log PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_log)
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build/dev --target test_common_log
# Expected: FAIL - straylight/log.h not found
```

- [ ] **Step 4: Implement log.h**

```cpp
// lib/common/include/straylight/log.h
#pragma once

#include <straylight/export.h>
#include <memory>
#include <string>
#include <string_view>

// Forward-declare spdlog types to avoid header pollution
namespace spdlog {
class logger;
}

namespace straylight {

/// Centralized logging facade wrapping spdlog.
/// Initialize once at startup, then use SL_* macros everywhere.
class STRAYLIGHT_EXPORT Log {
public:
    enum class Level { Trace, Debug, Info, Warn, Error, Critical, Off };

    /// Initialize the global logger. Call once in main().
    static void init(std::string_view app_name, Level level = Level::Info);

    /// Get the global logger instance.
    static std::shared_ptr<spdlog::logger> get();

    /// Create a named subsystem logger (inherits sinks from global).
    static std::shared_ptr<spdlog::logger> subsystem(std::string_view name);

    /// Set global log level at runtime.
    static void set_level(Level level);
};

} // namespace straylight

// Convenience macros — use these everywhere instead of spdlog directly.
#define SL_TRACE(...)    SPDLOG_LOGGER_TRACE(::straylight::Log::get(), __VA_ARGS__)
#define SL_DEBUG(...)    SPDLOG_LOGGER_DEBUG(::straylight::Log::get(), __VA_ARGS__)
#define SL_INFO(...)     SPDLOG_LOGGER_INFO(::straylight::Log::get(), __VA_ARGS__)
#define SL_WARN(...)     SPDLOG_LOGGER_WARN(::straylight::Log::get(), __VA_ARGS__)
#define SL_ERROR(...)    SPDLOG_LOGGER_ERROR(::straylight::Log::get(), __VA_ARGS__)
#define SL_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::straylight::Log::get(), __VA_ARGS__)
```

- [ ] **Step 5: Implement log.cpp**

```cpp
// lib/common/src/log.cpp
#include <straylight/log.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <mutex>

namespace straylight {

namespace {
std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_init_flag;

spdlog::level::level_enum to_spdlog(Log::Level level) {
    switch (level) {
        case Log::Level::Trace:    return spdlog::level::trace;
        case Log::Level::Debug:    return spdlog::level::debug;
        case Log::Level::Info:     return spdlog::level::info;
        case Log::Level::Warn:     return spdlog::level::warn;
        case Log::Level::Error:    return spdlog::level::err;
        case Log::Level::Critical: return spdlog::level::critical;
        case Log::Level::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}
} // namespace

void Log::init(std::string_view app_name, Level level) {
    // Allow re-init in tests
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");

    g_logger = std::make_shared<spdlog::logger>(
        std::string(app_name), console_sink);
    g_logger->set_level(to_spdlog(level));
    g_logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(g_logger);
}

std::shared_ptr<spdlog::logger> Log::get() {
    if (!g_logger) {
        // Lazy init with defaults if init() wasn't called
        init("straylight", Level::Info);
    }
    return g_logger;
}

std::shared_ptr<spdlog::logger> Log::subsystem(std::string_view name) {
    auto parent = get();
    auto sub = std::make_shared<spdlog::logger>(
        std::string(name), parent->sinks().begin(), parent->sinks().end());
    sub->set_level(parent->level());
    return sub;
}

void Log::set_level(Level level) {
    if (g_logger) {
        g_logger->set_level(to_spdlog(level));
    }
}

} // namespace straylight
```

- [ ] **Step 6: Update lib/common/CMakeLists.txt**

```cmake
find_package(spdlog 1.13 REQUIRED)

add_library(straylight-common SHARED
    src/log.cpp
)

target_include_directories(straylight-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-common
    PUBLIC spdlog::spdlog
)

set_target_properties(straylight-common PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-common PRIVATE straylight_common_EXPORTS)
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure -R common
# Expected: All Result + Log tests pass
```

- [ ] **Step 8: Commit**

```bash
git add lib/common/ tests/unit/common/test_log.cpp tests/unit/common/CMakeLists.txt
git commit -m "feat(common): add spdlog logging wrapper with SL_* macros"
```

---

### Task 5: libstraylight-common — JSON Config Loader

**Files:**
- Create: `lib/common/include/straylight/config.h`
- Create: `lib/common/src/config.cpp`
- Create: `tests/unit/common/test_config.cpp`
- Modify: `lib/common/CMakeLists.txt`
- Modify: `tests/unit/common/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

```cpp
// tests/unit/common/test_config.cpp
#include <gtest/gtest.h>
#include <straylight/config.h>
#include <fstream>
#include <filesystem>

using namespace straylight;

class ConfigTest : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir;

    void SetUp() override {
        tmp_dir = std::filesystem::temp_directory_path() / "straylight_test_config";
        std::filesystem::create_directories(tmp_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir);
    }

    void write_file(const std::string& name, const std::string& content) {
        std::ofstream f(tmp_dir / name);
        f << content;
    }
};

TEST_F(ConfigTest, LoadValidJson) {
    write_file("test.json", R"({"name": "straylight", "version": 1})");
    auto result = Config::load(tmp_dir / "test.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get<std::string>("name"), "straylight");
    EXPECT_EQ(result.value().get<int>("version"), 1);
}

TEST_F(ConfigTest, LoadMissingFileReturnsError) {
    auto result = Config::load(tmp_dir / "nonexistent.json");
    ASSERT_FALSE(result.has_value());
}

TEST_F(ConfigTest, LoadInvalidJsonReturnsError) {
    write_file("bad.json", "not json {{{");
    auto result = Config::load(tmp_dir / "bad.json");
    ASSERT_FALSE(result.has_value());
}

TEST_F(ConfigTest, GetWithDefaultReturnsFallback) {
    write_file("test.json", R"({"name": "straylight"})");
    auto result = Config::load(tmp_dir / "test.json");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().get<int>("missing_key", 42), 42);
}

TEST_F(ConfigTest, NestedAccess) {
    write_file("test.json", R"({"display": {"width": 1920, "height": 1080}})");
    auto result = Config::load(tmp_dir / "test.json");
    ASSERT_TRUE(result.has_value());
    auto& cfg = result.value();
    EXPECT_EQ(cfg.get<int>("display.width"), 1920);
    EXPECT_EQ(cfg.get<int>("display.height"), 1080);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/dev --target test_common_config
# Expected: FAIL - straylight/config.h not found
```

- [ ] **Step 3: Implement config.h**

```cpp
// lib/common/include/straylight/config.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace straylight {

/// JSON-based configuration loaded from file.
/// Supports dot-notation access for nested keys ("display.width").
class STRAYLIGHT_EXPORT Config {
public:
    /// Load a JSON config file. Returns error string on failure.
    static Result<Config, std::string> load(const std::filesystem::path& path);

    /// Get a typed value by key. Supports dot-notation for nesting.
    /// Throws if key not found and no default provided.
    template <typename T>
    T get(std::string_view key) const {
        const auto* node = resolve(key);
        if (!node || node->is_null()) {
            throw std::runtime_error(
                std::string("Config key not found: ") + std::string(key));
        }
        return node->get<T>();
    }

    /// Get a typed value with a default fallback.
    template <typename T>
    T get(std::string_view key, T default_val) const {
        const auto* node = resolve(key);
        if (!node || node->is_null()) {
            return default_val;
        }
        return node->get<T>();
    }

    /// Check if a key exists.
    [[nodiscard]] bool has(std::string_view key) const;

    /// Get the raw JSON object.
    [[nodiscard]] const nlohmann::json& raw() const { return data_; }

private:
    explicit Config(nlohmann::json data) : data_(std::move(data)) {}
    const nlohmann::json* resolve(std::string_view dotted_key) const;

    nlohmann::json data_;
};

} // namespace straylight
```

- [ ] **Step 4: Implement config.cpp**

```cpp
// lib/common/src/config.cpp
#include <straylight/config.h>
#include <straylight/log.h>

#include <fstream>
#include <sstream>

namespace straylight {

Result<Config, std::string> Config::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return Result<Config, std::string>::error(
            "Config file not found: " + path.string());
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return Result<Config, std::string>::error(
            "Cannot open config file: " + path.string());
    }

    try {
        auto data = nlohmann::json::parse(file);
        SL_DEBUG("Loaded config from {}", path.string());
        return Result<Config, std::string>::ok(Config(std::move(data)));
    } catch (const nlohmann::json::parse_error& e) {
        return Result<Config, std::string>::error(
            std::string("JSON parse error: ") + e.what());
    }
}

bool Config::has(std::string_view key) const {
    return resolve(key) != nullptr;
}

const nlohmann::json* Config::resolve(std::string_view dotted_key) const {
    const nlohmann::json* current = &data_;

    std::string key_str(dotted_key);
    std::istringstream stream(key_str);
    std::string segment;

    while (std::getline(stream, segment, '.')) {
        if (!current->is_object() || !current->contains(segment)) {
            return nullptr;
        }
        current = &(*current)[segment];
    }

    return current;
}

} // namespace straylight
```

- [ ] **Step 5: Update lib/common/CMakeLists.txt — add nlohmann_json + config.cpp**

```cmake
find_package(spdlog 1.13 REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

add_library(straylight-common SHARED
    src/log.cpp
    src/config.cpp
)

target_include_directories(straylight-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-common
    PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json
)

set_target_properties(straylight-common PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-common PRIVATE straylight_common_EXPORTS)
```

- [ ] **Step 6: Add test target, build, run**

Add to `tests/unit/common/CMakeLists.txt`:
```cmake
add_executable(test_common_config test_config.cpp)
target_link_libraries(test_common_config PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_config)
```

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure -R common
# Expected: All Result + Log + Config tests pass
```

- [ ] **Step 7: Commit**

```bash
git add lib/common/ tests/unit/common/
git commit -m "feat(common): add JSON config loader with dot-notation access"
```

---

### Task 6: libstraylight-common — IPC (Unix Domain Sockets)

**Files:**
- Create: `lib/common/include/straylight/ipc.h`
- Create: `lib/common/src/ipc.cpp`
- Create: `tests/unit/common/test_ipc.cpp`
- Modify: `lib/common/CMakeLists.txt`
- Modify: `tests/unit/common/CMakeLists.txt`

- [ ] **Step 1: Write failing test**

```cpp
// tests/unit/common/test_ipc.cpp
#include <gtest/gtest.h>
#include <straylight/ipc.h>

#include <filesystem>
#include <thread>
#include <chrono>

using namespace straylight;
using namespace std::chrono_literals;

class IpcTest : public ::testing::Test {
protected:
    std::filesystem::path sock_path;

    void SetUp() override {
        sock_path = std::filesystem::temp_directory_path() / "straylight_test.sock";
        std::filesystem::remove(sock_path);
    }

    void TearDown() override {
        std::filesystem::remove(sock_path);
    }
};

TEST_F(IpcTest, ServerAcceptsClient) {
    IpcServer server;
    auto bind_result = server.bind(sock_path.string());
    ASSERT_TRUE(bind_result.has_value()) << bind_result.error();

    std::thread client_thread([&]() {
        std::this_thread::sleep_for(50ms);
        IpcClient client;
        auto conn = client.connect(sock_path.string());
        ASSERT_TRUE(conn.has_value()) << conn.error();
    });

    auto conn = server.accept(1000);  // 1s timeout
    ASSERT_TRUE(conn.has_value()) << conn.error();

    client_thread.join();
}

TEST_F(IpcTest, SendAndReceiveMessage) {
    IpcServer server;
    server.bind(sock_path.string());

    std::string received;
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(50ms);
        IpcClient client;
        client.connect(sock_path.string());
        client.send(R"({"type":"ping"})");
        auto resp = client.receive();
        if (resp.has_value()) received = resp.value();
    });

    auto conn = server.accept(1000);
    ASSERT_TRUE(conn.has_value());

    auto msg = conn.value()->receive();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg.value(), R"({"type":"ping"})");

    conn.value()->send(R"({"type":"pong"})");

    client_thread.join();
    EXPECT_EQ(received, R"({"type":"pong"})");
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build/dev --target test_common_ipc
# Expected: FAIL - straylight/ipc.h not found
```

- [ ] **Step 3: Implement ipc.h**

```cpp
// lib/common/include/straylight/ipc.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>

#include <memory>
#include <string>
#include <string_view>

namespace straylight {

/// A connected IPC socket (either client or accepted server connection).
class STRAYLIGHT_EXPORT IpcConnection {
public:
    ~IpcConnection();
    IpcConnection(IpcConnection&&) noexcept;
    IpcConnection& operator=(IpcConnection&&) noexcept;

    /// Send a length-prefixed message.
    Result<void, std::string> send(std::string_view message);

    /// Receive a length-prefixed message. Blocks until data available.
    Result<std::string, std::string> receive();

    /// Get the underlying file descriptor (for epoll integration).
    [[nodiscard]] int fd() const noexcept;

private:
    friend class IpcServer;
    friend class IpcClient;
    explicit IpcConnection(int fd);

    int fd_ = -1;
};

/// Unix domain socket server.
class STRAYLIGHT_EXPORT IpcServer {
public:
    IpcServer();
    ~IpcServer();

    /// Bind to a socket path and listen.
    Result<void, std::string> bind(const std::string& path);

    /// Accept a connection. timeout_ms=0 means block forever.
    Result<std::unique_ptr<IpcConnection>, std::string> accept(int timeout_ms = 0);

private:
    int fd_ = -1;
    std::string path_;
};

/// Unix domain socket client.
class STRAYLIGHT_EXPORT IpcClient : public IpcConnection {
public:
    IpcClient();

    /// Connect to a server at the given socket path.
    Result<void, std::string> connect(const std::string& path);
};

// Specialization for void value type
template <typename E>
class Result<void, E> {
public:
    static Result ok() { return Result(true); }
    static Result error(E err) { return Result(std::move(err)); }
    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    [[nodiscard]] const E& error() const& { return err_; }
private:
    explicit Result(bool) : ok_(true) {}
    explicit Result(E err) : ok_(false), err_(std::move(err)) {}
    bool ok_ = false;
    E err_{};
};

} // namespace straylight
```

- [ ] **Step 4: Implement ipc.cpp**

```cpp
// lib/common/src/ipc.cpp
#include <straylight/ipc.h>
#include <straylight/log.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <vector>

namespace straylight {

// --- IpcConnection ---

IpcConnection::IpcConnection(int fd) : fd_(fd) {}

IpcConnection::~IpcConnection() {
    if (fd_ >= 0) ::close(fd_);
}

IpcConnection::IpcConnection(IpcConnection&& o) noexcept : fd_(o.fd_) {
    o.fd_ = -1;
}

IpcConnection& IpcConnection::operator=(IpcConnection&& o) noexcept {
    if (this != &o) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

int IpcConnection::fd() const noexcept { return fd_; }

Result<void, std::string> IpcConnection::send(std::string_view message) {
    uint32_t len = static_cast<uint32_t>(message.size());
    // Send length prefix (4 bytes, network order not needed for local IPC)
    if (::send(fd_, &len, sizeof(len), MSG_NOSIGNAL) != sizeof(len)) {
        return Result<void, std::string>::error("Failed to send message length");
    }
    size_t sent = 0;
    while (sent < message.size()) {
        auto n = ::send(fd_, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return Result<void, std::string>::error("Failed to send message body");
        }
        sent += static_cast<size_t>(n);
    }
    return Result<void, std::string>::ok();
}

Result<std::string, std::string> IpcConnection::receive() {
    uint32_t len = 0;
    auto n = ::recv(fd_, &len, sizeof(len), MSG_WAITALL);
    if (n != sizeof(len)) {
        return Result<std::string, std::string>::error("Failed to receive message length");
    }
    if (len > 16 * 1024 * 1024) {  // 16MB max message
        return Result<std::string, std::string>::error("Message too large");
    }
    std::string buf(len, '\0');
    size_t received = 0;
    while (received < len) {
        auto r = ::recv(fd_, buf.data() + received, len - received, 0);
        if (r <= 0) {
            return Result<std::string, std::string>::error("Failed to receive message body");
        }
        received += static_cast<size_t>(r);
    }
    return Result<std::string, std::string>::ok(std::move(buf));
}

// --- IpcServer ---

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    if (fd_ >= 0) {
        ::close(fd_);
        if (!path_.empty()) ::unlink(path_.c_str());
    }
}

Result<void, std::string> IpcServer::bind(const std::string& path) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return Result<void, std::string>::error("socket() failed");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    ::unlink(path.c_str());

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return Result<void, std::string>::error("bind() failed: " + std::string(strerror(errno)));
    }

    if (::listen(fd_, 16) < 0) {
        return Result<void, std::string>::error("listen() failed");
    }

    path_ = path;
    SL_DEBUG("IPC server listening on {}", path);
    return Result<void, std::string>::ok();
}

Result<std::unique_ptr<IpcConnection>, std::string> IpcServer::accept(int timeout_ms) {
    if (timeout_ms > 0) {
        pollfd pfd{fd_, POLLIN, 0};
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret == 0) {
            return Result<std::unique_ptr<IpcConnection>, std::string>::error("accept() timed out");
        }
        if (ret < 0) {
            return Result<std::unique_ptr<IpcConnection>, std::string>::error("poll() failed");
        }
    }

    int client_fd = ::accept(fd_, nullptr, nullptr);
    if (client_fd < 0) {
        return Result<std::unique_ptr<IpcConnection>, std::string>::error("accept() failed");
    }

    return Result<std::unique_ptr<IpcConnection>, std::string>::ok(
        std::unique_ptr<IpcConnection>(new IpcConnection(client_fd)));
}

// --- IpcClient ---

IpcClient::IpcClient() : IpcConnection(-1) {}

Result<void, std::string> IpcClient::connect(const std::string& path) {
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return Result<void, std::string>::error("socket() failed");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return Result<void, std::string>::error("connect() failed: " + std::string(strerror(errno)));
    }

    SL_DEBUG("IPC client connected to {}", path);
    return Result<void, std::string>::ok();
}

} // namespace straylight
```

- [ ] **Step 5: Update CMakeLists files**

Update `lib/common/CMakeLists.txt` — add `src/ipc.cpp` to sources.

Add to `tests/unit/common/CMakeLists.txt`:
```cmake
add_executable(test_common_ipc test_ipc.cpp)
target_link_libraries(test_common_ipc PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_ipc)
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure -R common
# Expected: All tests pass (Result + Log + Config + IPC)
```

- [ ] **Step 7: Commit**

```bash
git add lib/common/ tests/unit/common/
git commit -m "feat(common): add Unix domain socket IPC client/server"
```

---

### Task 7: libstraylight-common — Types and Umbrella Header

**Files:**
- Create: `lib/common/include/straylight/types.h`
- Create: `lib/common/include/straylight/common.h`

- [ ] **Step 1: Create types.h**

```cpp
// lib/common/include/straylight/types.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace straylight {

/// Data types for tensor elements.
enum class DType : uint8_t {
    Float16 = 0,
    Float32 = 1,
    Float64 = 2,
    Int8 = 3,
    Int16 = 4,
    Int32 = 5,
    Int64 = 6,
    UInt8 = 7,
    BFloat16 = 8,
    Float8E4M3 = 9,
    Float8E5M2 = 10,
};

/// Size in bytes of a single element of the given dtype.
constexpr size_t dtype_size(DType dt) {
    switch (dt) {
        case DType::Float8E4M3:
        case DType::Float8E5M2:
        case DType::Int8:
        case DType::UInt8:     return 1;
        case DType::Float16:
        case DType::BFloat16:
        case DType::Int16:     return 2;
        case DType::Float32:
        case DType::Int32:     return 4;
        case DType::Float64:
        case DType::Int64:     return 8;
    }
    return 0;
}

/// Compute device types.
enum class DeviceType : uint8_t {
    CPU = 0,
    CUDA = 1,
    ROCm = 2,
    OneAPI = 3,
    Metal = 4,  // Not supported at runtime, kept for format compat
};

/// Describes a tensor's metadata (shape, dtype, device) without owning data.
struct TensorDesc {
    std::vector<int64_t> shape;
    DType dtype = DType::Float32;
    DeviceType device = DeviceType::CPU;
    int device_id = 0;

    /// Total number of elements.
    [[nodiscard]] int64_t numel() const {
        int64_t n = 1;
        for (auto s : shape) n *= s;
        return n;
    }

    /// Total size in bytes.
    [[nodiscard]] size_t nbytes() const {
        return static_cast<size_t>(numel()) * dtype_size(dtype);
    }
};

/// Subsystem health status.
enum class HealthStatus : uint8_t {
    Ok = 0,
    Degraded = 1,
    Error = 2,
    Unknown = 3,
};

} // namespace straylight
```

- [ ] **Step 2: Create umbrella header common.h**

```cpp
// lib/common/include/straylight/common.h
#pragma once

/// Umbrella header for libstraylight-common.
/// Include this one header to get all common utilities.

#include <straylight/version.h>
#include <straylight/export.h>
#include <straylight/result.h>
#include <straylight/log.h>
#include <straylight/config.h>
#include <straylight/ipc.h>
#include <straylight/types.h>
```

- [ ] **Step 3: Commit**

```bash
git add lib/common/include/straylight/
git commit -m "feat(common): add types (DType, DeviceType, TensorDesc) and umbrella header"
```

---

## Chunk 2: ML, Net, and HW Libraries

### Task 8: libstraylight-ml — Tensor and Graph Types

**Files:**
- Create: `lib/ml/include/straylight/ml/tensor.h`
- Create: `lib/ml/include/straylight/ml/graph.h`
- Create: `lib/ml/include/straylight/ml/kv_cache.h`
- Create: `lib/ml/src/tensor.cpp`
- Create: `lib/ml/src/graph.cpp`
- Create: `lib/ml/src/kv_cache.cpp`
- Create: `tests/unit/ml/test_tensor.cpp`
- Create: `tests/unit/ml/test_graph.cpp`
- Create: `tests/unit/ml/test_kv_cache.cpp`
- Create: `tests/unit/ml/CMakeLists.txt`
- Modify: `lib/ml/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for Tensor**

```cpp
// tests/unit/ml/test_tensor.cpp
#include <gtest/gtest.h>
#include <straylight/ml/tensor.h>

using namespace straylight;
using namespace straylight::ml;

TEST(TensorTest, CreateFromShape) {
    Tensor t({2, 3, 4}, DType::Float32);
    EXPECT_EQ(t.numel(), 24);
    EXPECT_EQ(t.nbytes(), 96);
    EXPECT_EQ(t.ndim(), 3);
    EXPECT_EQ(t.shape()[0], 2);
}

TEST(TensorTest, DataPointerIsValid) {
    Tensor t({10}, DType::Float32);
    ASSERT_NE(t.data(), nullptr);
    // Write and read back
    auto* ptr = static_cast<float*>(t.data());
    ptr[0] = 3.14f;
    EXPECT_FLOAT_EQ(ptr[0], 3.14f);
}

TEST(TensorTest, MoveSemantics) {
    Tensor a({4, 4}, DType::Float32);
    auto* ptr = a.data();
    Tensor b = std::move(a);
    EXPECT_EQ(b.data(), ptr);
    EXPECT_EQ(a.data(), nullptr);
}

TEST(TensorTest, DescReturnsCorrectMetadata) {
    Tensor t({8, 16}, DType::Int8, DeviceType::CPU);
    auto desc = t.desc();
    EXPECT_EQ(desc.shape.size(), 2u);
    EXPECT_EQ(desc.dtype, DType::Int8);
    EXPECT_EQ(desc.nbytes(), 128u);
}
```

- [ ] **Step 2: Implement Tensor**

```cpp
// lib/ml/include/straylight/ml/tensor.h
#pragma once

#include <straylight/export.h>
#include <straylight/types.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace straylight::ml {

/// Owning CPU tensor with contiguous memory.
/// For GPU tensors, see libstraylight-hw VPU allocator.
class STRAYLIGHT_EXPORT Tensor {
public:
    /// Allocate a new zero-initialized tensor.
    explicit Tensor(std::vector<int64_t> shape, DType dtype = DType::Float32,
                    DeviceType device = DeviceType::CPU);

    ~Tensor();

    // Move-only
    Tensor(Tensor&& other) noexcept;
    Tensor& operator=(Tensor&& other) noexcept;
    Tensor(const Tensor&) = delete;
    Tensor& operator=(const Tensor&) = delete;

    /// Raw data pointer. Returns nullptr if moved-from.
    [[nodiscard]] void* data() noexcept { return data_.get(); }
    [[nodiscard]] const void* data() const noexcept { return data_.get(); }

    /// Typed data access.
    template <typename T>
    T* typed_data() noexcept { return static_cast<T*>(data_.get()); }

    [[nodiscard]] const std::vector<int64_t>& shape() const noexcept { return desc_.shape; }
    [[nodiscard]] int64_t numel() const noexcept { return desc_.numel(); }
    [[nodiscard]] size_t nbytes() const noexcept { return desc_.nbytes(); }
    [[nodiscard]] size_t ndim() const noexcept { return desc_.shape.size(); }
    [[nodiscard]] DType dtype() const noexcept { return desc_.dtype; }
    [[nodiscard]] TensorDesc desc() const { return desc_; }

private:
    TensorDesc desc_;
    std::unique_ptr<uint8_t[]> data_;
};

} // namespace straylight::ml
```

```cpp
// lib/ml/src/tensor.cpp
#include <straylight/ml/tensor.h>
#include <cstring>

namespace straylight::ml {

Tensor::Tensor(std::vector<int64_t> shape, DType dtype, DeviceType device)
    : desc_{std::move(shape), dtype, device, 0} {
    auto bytes = desc_.nbytes();
    data_ = std::make_unique<uint8_t[]>(bytes);
    std::memset(data_.get(), 0, bytes);
}

Tensor::~Tensor() = default;

Tensor::Tensor(Tensor&& other) noexcept
    : desc_(std::move(other.desc_)), data_(std::move(other.data_)) {}

Tensor& Tensor::operator=(Tensor&& other) noexcept {
    if (this != &other) {
        desc_ = std::move(other.desc_);
        data_ = std::move(other.data_);
    }
    return *this;
}

} // namespace straylight::ml
```

- [ ] **Step 3: Write failing tests for Graph IR**

```cpp
// tests/unit/ml/test_graph.cpp
#include <gtest/gtest.h>
#include <straylight/ml/graph.h>

using namespace straylight::ml;

TEST(GraphTest, CreateEmptyGraph) {
    Graph g("test_model");
    EXPECT_EQ(g.name(), "test_model");
    EXPECT_EQ(g.num_nodes(), 0u);
}

TEST(GraphTest, AddNodesAndEdges) {
    Graph g("matmul_chain");
    auto input = g.add_input("x", {1, 768});
    auto weight = g.add_input("w", {768, 768});
    auto matmul = g.add_op("MatMul", {input, weight}, "mm0");
    auto relu = g.add_op("ReLU", {matmul}, "relu0");

    EXPECT_EQ(g.num_nodes(), 4u);
    EXPECT_EQ(g.node(matmul).inputs.size(), 2u);
    EXPECT_EQ(g.node(relu).inputs.size(), 1u);
    EXPECT_EQ(g.node(relu).inputs[0], matmul);
}

TEST(GraphTest, TopologicalOrder) {
    Graph g("topo_test");
    auto a = g.add_input("a", {1});
    auto b = g.add_op("Neg", {a}, "neg");
    auto c = g.add_op("Abs", {b}, "abs");

    auto order = g.topological_order();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], a);
    EXPECT_EQ(order[1], b);
    EXPECT_EQ(order[2], c);
}
```

- [ ] **Step 4: Implement Graph**

```cpp
// lib/ml/include/straylight/ml/graph.h
#pragma once

#include <straylight/export.h>
#include <straylight/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace straylight::ml {

using NodeId = uint32_t;

struct GraphNode {
    NodeId id;
    std::string name;
    std::string op_type;  // "Input", "MatMul", "ReLU", etc.
    std::vector<NodeId> inputs;
    std::vector<int64_t> output_shape;
};

/// Directed acyclic graph representing a computation.
class STRAYLIGHT_EXPORT Graph {
public:
    explicit Graph(std::string name);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] size_t num_nodes() const noexcept { return nodes_.size(); }

    /// Add an input node.
    NodeId add_input(std::string name, std::vector<int64_t> shape);

    /// Add an operation node with input dependencies.
    NodeId add_op(std::string op_type, std::vector<NodeId> inputs, std::string name = "");

    /// Get a node by ID.
    [[nodiscard]] const GraphNode& node(NodeId id) const;

    /// Return nodes in topological order.
    [[nodiscard]] std::vector<NodeId> topological_order() const;

private:
    std::string name_;
    std::vector<GraphNode> nodes_;
};

} // namespace straylight::ml
```

```cpp
// lib/ml/src/graph.cpp
#include <straylight/ml/graph.h>

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace straylight::ml {

Graph::Graph(std::string name) : name_(std::move(name)) {}

NodeId Graph::add_input(std::string name, std::vector<int64_t> shape) {
    NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back({id, std::move(name), "Input", {}, std::move(shape)});
    return id;
}

NodeId Graph::add_op(std::string op_type, std::vector<NodeId> inputs, std::string name) {
    NodeId id = static_cast<NodeId>(nodes_.size());
    if (name.empty()) {
        name = op_type + "_" + std::to_string(id);
    }
    nodes_.push_back({id, std::move(name), std::move(op_type), std::move(inputs), {}});
    return id;
}

const GraphNode& Graph::node(NodeId id) const {
    if (id >= nodes_.size()) {
        throw std::out_of_range("Invalid node ID");
    }
    return nodes_[id];
}

std::vector<NodeId> Graph::topological_order() const {
    // Kahn's algorithm
    std::unordered_map<NodeId, int> in_degree;
    for (auto& n : nodes_) {
        if (in_degree.find(n.id) == in_degree.end()) in_degree[n.id] = 0;
        for (auto dep : n.inputs) {
            in_degree[n.id]++;
        }
    }

    std::queue<NodeId> q;
    for (auto& [id, deg] : in_degree) {
        if (deg == 0) q.push(id);
    }

    std::vector<NodeId> order;
    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        order.push_back(cur);

        for (auto& n : nodes_) {
            for (auto dep : n.inputs) {
                if (dep == cur) {
                    if (--in_degree[n.id] == 0) {
                        q.push(n.id);
                    }
                }
            }
        }
    }

    return order;
}

} // namespace straylight::ml
```

- [ ] **Step 5: Write failing tests for KV Cache**

```cpp
// tests/unit/ml/test_kv_cache.cpp
#include <gtest/gtest.h>
#include <straylight/ml/kv_cache.h>

using namespace straylight::ml;

TEST(KvCacheTest, PutAndGet) {
    KvCache cache(3);  // max 3 entries
    cache.put("key1", Tensor({1, 4}, straylight::DType::Float32));
    auto* t = cache.get("key1");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->numel(), 4);
}

TEST(KvCacheTest, MissReturnsNull) {
    KvCache cache(3);
    EXPECT_EQ(cache.get("nonexistent"), nullptr);
}

TEST(KvCacheTest, EvictsLRU) {
    KvCache cache(2);  // max 2 entries
    cache.put("a", Tensor({1}, straylight::DType::Float32));
    cache.put("b", Tensor({2}, straylight::DType::Float32));
    cache.get("a");  // touch "a" so "b" is LRU
    cache.put("c", Tensor({3}, straylight::DType::Float32));  // should evict "b"

    EXPECT_NE(cache.get("a"), nullptr);
    EXPECT_EQ(cache.get("b"), nullptr);  // evicted
    EXPECT_NE(cache.get("c"), nullptr);
}

TEST(KvCacheTest, SizeTracking) {
    KvCache cache(10);
    EXPECT_EQ(cache.size(), 0u);
    cache.put("x", Tensor({4}, straylight::DType::Float32));
    EXPECT_EQ(cache.size(), 1u);
}
```

- [ ] **Step 6: Implement KV Cache**

```cpp
// lib/ml/include/straylight/ml/kv_cache.h
#pragma once

#include <straylight/export.h>
#include <straylight/ml/tensor.h>

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>

namespace straylight::ml {

/// LRU cache for KV tensors (inference state reuse).
class STRAYLIGHT_EXPORT KvCache {
public:
    explicit KvCache(size_t max_entries);

    /// Insert or replace a tensor in the cache.
    void put(const std::string& key, Tensor value);

    /// Retrieve a tensor. Returns nullptr on miss. Marks as recently used.
    Tensor* get(const std::string& key);

    /// Number of entries currently in cache.
    [[nodiscard]] size_t size() const noexcept { return map_.size(); }

    /// Remove all entries.
    void clear();

private:
    void evict();

    size_t max_entries_;

    // LRU list: front = most recent, back = least recent
    using ListEntry = std::pair<std::string, Tensor>;
    std::list<ListEntry> lru_list_;
    std::unordered_map<std::string, std::list<ListEntry>::iterator> map_;
};

} // namespace straylight::ml
```

```cpp
// lib/ml/src/kv_cache.cpp
#include <straylight/ml/kv_cache.h>

namespace straylight::ml {

KvCache::KvCache(size_t max_entries) : max_entries_(max_entries) {}

void KvCache::put(const std::string& key, Tensor value) {
    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_list_.erase(it->second);
        map_.erase(it);
    }

    if (map_.size() >= max_entries_) {
        evict();
    }

    lru_list_.emplace_front(key, std::move(value));
    map_[key] = lru_list_.begin();
}

Tensor* KvCache::get(const std::string& key) {
    auto it = map_.find(key);
    if (it == map_.end()) return nullptr;

    // Move to front (most recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    return &it->second->second;
}

void KvCache::clear() {
    lru_list_.clear();
    map_.clear();
}

void KvCache::evict() {
    if (lru_list_.empty()) return;
    auto& back = lru_list_.back();
    map_.erase(back.first);
    lru_list_.pop_back();
}

} // namespace straylight::ml
```

- [ ] **Step 7: Update lib/ml/CMakeLists.txt**

```cmake
add_library(straylight-ml SHARED
    src/tensor.cpp
    src/graph.cpp
    src/kv_cache.cpp
)

target_include_directories(straylight-ml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-ml PUBLIC straylight-common)

set_target_properties(straylight-ml PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-ml PRIVATE straylight_ml_EXPORTS)
```

- [ ] **Step 8: Create tests/unit/ml/CMakeLists.txt and register**

```cmake
# tests/unit/ml/CMakeLists.txt
add_executable(test_ml_tensor test_tensor.cpp)
target_link_libraries(test_ml_tensor PRIVATE straylight-ml GTest::gtest_main)
gtest_discover_tests(test_ml_tensor)

add_executable(test_ml_graph test_graph.cpp)
target_link_libraries(test_ml_graph PRIVATE straylight-ml GTest::gtest_main)
gtest_discover_tests(test_ml_graph)

add_executable(test_ml_kv_cache test_kv_cache.cpp)
target_link_libraries(test_ml_kv_cache PRIVATE straylight-ml GTest::gtest_main)
gtest_discover_tests(test_ml_kv_cache)
```

Add to `tests/CMakeLists.txt`:
```cmake
add_subdirectory(unit/ml)
```

- [ ] **Step 9: Build and run all tests**

```bash
cmake --preset dev
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
# Expected: All common + ml tests pass
```

- [ ] **Step 10: Commit**

```bash
git add lib/ml/ tests/unit/ml/ tests/CMakeLists.txt
git commit -m "feat(ml): add Tensor, Graph IR, and LRU KV Cache"
```

---

### Task 9: libstraylight-net — Socket Abstraction and Transport Protocol

**Files:**
- Create: `lib/net/include/straylight/net/socket.h`
- Create: `lib/net/include/straylight/net/transport.h`
- Create: `lib/net/include/straylight/net/protocol.h`
- Create: `lib/net/src/socket.cpp`
- Create: `lib/net/src/transport.cpp`
- Create: `tests/unit/net/test_socket.cpp`
- Create: `tests/unit/net/test_transport.cpp`
- Create: `tests/unit/net/CMakeLists.txt`
- Modify: `lib/net/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test for UDP socket**

```cpp
// tests/unit/net/test_socket.cpp
#include <gtest/gtest.h>
#include <straylight/net/socket.h>
#include <thread>
#include <chrono>

using namespace straylight::net;
using namespace std::chrono_literals;

TEST(UdpSocketTest, SendAndReceive) {
    UdpSocket server;
    auto bind_result = server.bind("127.0.0.1", 0);  // 0 = ephemeral port
    ASSERT_TRUE(bind_result.has_value()) << bind_result.error();
    auto port = server.local_port();

    std::thread sender([port]() {
        std::this_thread::sleep_for(50ms);
        UdpSocket client;
        client.send_to("127.0.0.1", port, "hello", 5);
    });

    uint8_t buf[64];
    auto result = server.recv_from(buf, sizeof(buf), 1000);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result.value().bytes_received, 5u);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), 5), "hello");

    sender.join();
}
```

- [ ] **Step 2: Implement socket.h and socket.cpp**

```cpp
// lib/net/include/straylight/net/socket.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace straylight::net {

struct RecvResult {
    size_t bytes_received;
    std::string sender_addr;
    uint16_t sender_port;
};

/// UDP socket for datagram-based tensor transport.
class STRAYLIGHT_EXPORT UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;

    /// Bind to address and port (port 0 = OS picks ephemeral).
    straylight::Result<void, std::string> bind(const std::string& addr, uint16_t port);

    /// Send data to a specific address.
    straylight::Result<size_t, std::string> send_to(
        const std::string& addr, uint16_t port,
        const void* data, size_t len);

    /// Receive data. timeout_ms=0 blocks forever.
    straylight::Result<RecvResult, std::string> recv_from(
        void* buf, size_t buf_len, int timeout_ms = 0);

    /// Get the port we're bound to.
    [[nodiscard]] uint16_t local_port() const;

    [[nodiscard]] int fd() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

} // namespace straylight::net
```

```cpp
// lib/net/src/socket.cpp
#include <straylight/net/socket.h>
#include <straylight/log.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

namespace straylight::net {

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) ::close(fd_);
}

UdpSocket::UdpSocket(UdpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

UdpSocket& UdpSocket::operator=(UdpSocket&& o) noexcept {
    if (this != &o) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

straylight::Result<void, std::string> UdpSocket::bind(const std::string& addr, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return straylight::Result<void, std::string>::error("socket() failed");

    int opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, addr.c_str(), &sa.sin_addr);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        return straylight::Result<void, std::string>::error(
            std::string("bind() failed: ") + strerror(errno));
    }

    return straylight::Result<void, std::string>::ok();
}

straylight::Result<size_t, std::string> UdpSocket::send_to(
    const std::string& addr, uint16_t port, const void* data, size_t len) {
    if (fd_ < 0) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return straylight::Result<size_t, std::string>::error("socket() failed");
    }

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, addr.c_str(), &sa.sin_addr);

    auto sent = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (sent < 0) {
        return straylight::Result<size_t, std::string>::error("sendto() failed");
    }
    return straylight::Result<size_t, std::string>::ok(static_cast<size_t>(sent));
}

straylight::Result<RecvResult, std::string> UdpSocket::recv_from(
    void* buf, size_t buf_len, int timeout_ms) {
    if (timeout_ms > 0) {
        pollfd pfd{fd_, POLLIN, 0};
        int ret = ::poll(&pfd, 1, timeout_ms);
        if (ret == 0) return straylight::Result<RecvResult, std::string>::error("recv timeout");
        if (ret < 0) return straylight::Result<RecvResult, std::string>::error("poll() failed");
    }

    sockaddr_in sa{};
    socklen_t sa_len = sizeof(sa);
    auto n = ::recvfrom(fd_, buf, buf_len, 0, reinterpret_cast<sockaddr*>(&sa), &sa_len);
    if (n < 0) {
        return straylight::Result<RecvResult, std::string>::error("recvfrom() failed");
    }

    char addr_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sa.sin_addr, addr_buf, sizeof(addr_buf));

    return straylight::Result<RecvResult, std::string>::ok(
        RecvResult{static_cast<size_t>(n), addr_buf, ntohs(sa.sin_port)});
}

uint16_t UdpSocket::local_port() const {
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    getsockname(fd_, reinterpret_cast<sockaddr*>(&sa), &len);
    return ntohs(sa.sin_port);
}

} // namespace straylight::net
```

- [ ] **Step 3: Create protocol.h (wire format)**

```cpp
// lib/net/include/straylight/net/protocol.h
#pragma once

#include <straylight/types.h>
#include <cstdint>

namespace straylight::net {

/// Wire protocol header for tensor transport.
/// All multi-byte fields are little-endian.
struct __attribute__((packed)) TensorHeader {
    uint32_t magic = 0x53544C54;  // "STLT"
    uint16_t version = 1;
    uint8_t dtype;
    uint8_t ndim;
    uint64_t data_size;     // payload bytes
    int64_t shape[8];       // up to 8 dimensions, unused set to 0
};

static_assert(sizeof(TensorHeader) == 4 + 2 + 1 + 1 + 8 + 64, "Packed header size");

/// Message types for the StrayLight IPC protocol.
enum class MessageType : uint8_t {
    Ping = 0,
    Pong = 1,
    TensorPublish = 10,
    TensorSubscribe = 11,
    TensorData = 12,
    RegistryGet = 20,
    RegistrySet = 21,
    RegistryValue = 22,
    HealthCheck = 30,
    HealthReport = 31,
};

} // namespace straylight::net
```

- [ ] **Step 4: Create transport.h/cpp (tensor send/recv over UDP)**

```cpp
// lib/net/include/straylight/net/transport.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>
#include <straylight/net/socket.h>
#include <straylight/net/protocol.h>
#include <straylight/types.h>

#include <string>

namespace straylight::net {

/// Send a tensor descriptor + data over UDP.
STRAYLIGHT_EXPORT
straylight::Result<void, std::string> send_tensor(
    UdpSocket& sock,
    const std::string& dest_addr, uint16_t dest_port,
    const TensorDesc& desc,
    const void* data);

/// Receive a tensor header. Caller must then recv the data payload.
STRAYLIGHT_EXPORT
straylight::Result<TensorHeader, std::string> recv_tensor_header(
    UdpSocket& sock,
    int timeout_ms = 0);

} // namespace straylight::net
```

```cpp
// lib/net/src/transport.cpp
#include <straylight/net/transport.h>
#include <cstring>

namespace straylight::net {

straylight::Result<void, std::string> send_tensor(
    UdpSocket& sock,
    const std::string& dest_addr, uint16_t dest_port,
    const TensorDesc& desc,
    const void* data) {

    TensorHeader hdr{};
    hdr.dtype = static_cast<uint8_t>(desc.dtype);
    hdr.ndim = static_cast<uint8_t>(desc.shape.size());
    hdr.data_size = desc.nbytes();
    for (size_t i = 0; i < desc.shape.size() && i < 8; i++) {
        hdr.shape[i] = desc.shape[i];
    }

    // Send header
    auto r1 = sock.send_to(dest_addr, dest_port, &hdr, sizeof(hdr));
    if (!r1.has_value()) return straylight::Result<void, std::string>::error(r1.error());

    // Send data (for UDP, this works for small tensors; large tensors need chunking)
    if (data && hdr.data_size > 0) {
        auto r2 = sock.send_to(dest_addr, dest_port, data, hdr.data_size);
        if (!r2.has_value()) return straylight::Result<void, std::string>::error(r2.error());
    }

    return straylight::Result<void, std::string>::ok();
}

straylight::Result<TensorHeader, std::string> recv_tensor_header(
    UdpSocket& sock, int timeout_ms) {
    TensorHeader hdr{};
    auto r = sock.recv_from(&hdr, sizeof(hdr), timeout_ms);
    if (!r.has_value()) {
        return straylight::Result<TensorHeader, std::string>::error(r.error());
    }
    if (hdr.magic != 0x53544C54) {
        return straylight::Result<TensorHeader, std::string>::error("Invalid tensor header magic");
    }
    return straylight::Result<TensorHeader, std::string>::ok(hdr);
}

} // namespace straylight::net
```

- [ ] **Step 5: Update lib/net/CMakeLists.txt**

```cmake
add_library(straylight-net SHARED
    src/socket.cpp
    src/transport.cpp
)

target_include_directories(straylight-net
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-net PUBLIC straylight-common)

set_target_properties(straylight-net PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-net PRIVATE straylight_net_EXPORTS)
```

- [ ] **Step 6: Create tests, build, run**

```cmake
# tests/unit/net/CMakeLists.txt
add_executable(test_net_socket test_socket.cpp)
target_link_libraries(test_net_socket PRIVATE straylight-net GTest::gtest_main)
gtest_discover_tests(test_net_socket)
```

Add to `tests/CMakeLists.txt`: `add_subdirectory(unit/net)`

```bash
cmake --preset dev && cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
# Expected: All tests pass
```

- [ ] **Step 7: Commit**

```bash
git add lib/net/ tests/unit/net/ tests/CMakeLists.txt
git commit -m "feat(net): add UDP socket, wire protocol, and tensor transport"
```

---

### Task 10: libstraylight-hw — GPU Allocator and Entropy

**Files:**
- Create: `lib/hw/include/straylight/hw/gpu.h`
- Create: `lib/hw/include/straylight/hw/entropy.h`
- Create: `lib/hw/src/gpu.cpp`
- Create: `lib/hw/src/entropy.cpp`
- Create: `tests/unit/hw/test_gpu.cpp`
- Create: `tests/unit/hw/test_entropy.cpp`
- Create: `tests/unit/hw/CMakeLists.txt`
- Modify: `lib/hw/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing test for GPU allocator (CPU fallback)**

```cpp
// tests/unit/hw/test_gpu.cpp
#include <gtest/gtest.h>
#include <straylight/hw/gpu.h>

using namespace straylight::hw;

TEST(GpuAllocatorTest, AllocateAndFree) {
    GpuAllocator alloc(GpuBackend::CPU);  // CPU fallback for testing
    auto result = alloc.allocate(1024);
    ASSERT_TRUE(result.has_value()) << result.error();
    auto ptr = result.value();
    ASSERT_NE(ptr, nullptr);

    // Should be able to write to it
    std::memset(ptr, 0xAB, 1024);

    alloc.free(ptr);
}

TEST(GpuAllocatorTest, StatsTracking) {
    GpuAllocator alloc(GpuBackend::CPU);
    auto p1 = alloc.allocate(512).value();
    auto p2 = alloc.allocate(256).value();

    auto stats = alloc.stats();
    EXPECT_EQ(stats.allocations, 2u);
    EXPECT_EQ(stats.bytes_allocated, 768u);

    alloc.free(p1);
    alloc.free(p2);

    stats = alloc.stats();
    EXPECT_EQ(stats.bytes_allocated, 0u);
}
```

- [ ] **Step 2: Implement GPU allocator**

```cpp
// lib/hw/include/straylight/hw/gpu.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace straylight::hw {

enum class GpuBackend : uint8_t {
    CPU = 0,    // Fallback: uses malloc
    CUDA = 1,
    ROCm = 2,
    OneAPI = 3,
};

struct AllocStats {
    size_t allocations = 0;
    size_t bytes_allocated = 0;
    size_t peak_bytes = 0;
};

/// Slab-style GPU memory allocator. Falls back to CPU malloc when no GPU.
class STRAYLIGHT_EXPORT GpuAllocator {
public:
    explicit GpuAllocator(GpuBackend backend = GpuBackend::CPU);
    ~GpuAllocator();

    /// Allocate bytes on the configured device.
    straylight::Result<void*, std::string> allocate(size_t bytes);

    /// Free a previously allocated pointer.
    void free(void* ptr);

    /// Current allocation statistics.
    [[nodiscard]] AllocStats stats() const;

    [[nodiscard]] GpuBackend backend() const noexcept { return backend_; }

private:
    GpuBackend backend_;
    mutable std::mutex mu_;
    std::unordered_map<void*, size_t> allocs_;
    AllocStats stats_;
};

} // namespace straylight::hw
```

```cpp
// lib/hw/src/gpu.cpp
#include <straylight/hw/gpu.h>
#include <straylight/log.h>

#include <cstdlib>

namespace straylight::hw {

GpuAllocator::GpuAllocator(GpuBackend backend) : backend_(backend) {
    SL_DEBUG("GpuAllocator created with backend {}", static_cast<int>(backend));
}

GpuAllocator::~GpuAllocator() {
    std::lock_guard lock(mu_);
    for (auto& [ptr, size] : allocs_) {
        if (backend_ == GpuBackend::CPU) {
            std::free(ptr);
        }
        // TODO: CUDA/ROCm/OneAPI free paths
    }
}

straylight::Result<void*, std::string> GpuAllocator::allocate(size_t bytes) {
    std::lock_guard lock(mu_);

    void* ptr = nullptr;
    switch (backend_) {
        case GpuBackend::CPU:
            ptr = std::aligned_alloc(64, bytes);  // 64-byte aligned for SIMD
            break;
        case GpuBackend::CUDA:
        case GpuBackend::ROCm:
        case GpuBackend::OneAPI:
            // TODO: real GPU allocation
            ptr = std::aligned_alloc(64, bytes);
            break;
    }

    if (!ptr) {
        return straylight::Result<void*, std::string>::error("Allocation failed");
    }

    allocs_[ptr] = bytes;
    stats_.allocations++;
    stats_.bytes_allocated += bytes;
    if (stats_.bytes_allocated > stats_.peak_bytes) {
        stats_.peak_bytes = stats_.bytes_allocated;
    }

    return straylight::Result<void*, std::string>::ok(ptr);
}

void GpuAllocator::free(void* ptr) {
    std::lock_guard lock(mu_);
    auto it = allocs_.find(ptr);
    if (it == allocs_.end()) return;

    stats_.bytes_allocated -= it->second;
    allocs_.erase(it);

    if (backend_ == GpuBackend::CPU) {
        std::free(ptr);
    }
    // TODO: CUDA/ROCm/OneAPI free paths
}

AllocStats GpuAllocator::stats() const {
    std::lock_guard lock(mu_);
    return stats_;
}

} // namespace straylight::hw
```

- [ ] **Step 3: Write failing test for entropy**

```cpp
// tests/unit/hw/test_entropy.cpp
#include <gtest/gtest.h>
#include <straylight/hw/entropy.h>

#include <set>

using namespace straylight::hw;

TEST(EntropyTest, GenerateRandomBytes) {
    EntropySource src;
    uint8_t buf[32] = {};
    auto result = src.fill(buf, sizeof(buf));
    ASSERT_TRUE(result.has_value()) << result.error();

    // Should not be all zeros (astronomically unlikely)
    bool all_zero = true;
    for (auto b : buf) { if (b != 0) { all_zero = false; break; } }
    EXPECT_FALSE(all_zero);
}

TEST(EntropyTest, GenerateDistinctValues) {
    EntropySource src;
    std::set<uint64_t> values;
    for (int i = 0; i < 100; i++) {
        uint64_t v = 0;
        src.fill(&v, sizeof(v));
        values.insert(v);
    }
    // At least 90 distinct values out of 100 (near certain for good RNG)
    EXPECT_GE(values.size(), 90u);
}

TEST(EntropyTest, HealthCheckPasses) {
    EntropySource src;
    EXPECT_TRUE(src.health_check().has_value());
}
```

- [ ] **Step 4: Implement entropy.h and entropy.cpp**

```cpp
// lib/hw/include/straylight/hw/entropy.h
#pragma once

#include <straylight/export.h>
#include <straylight/result.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace straylight::hw {

/// Hardware entropy source. Uses RDRAND/RDSEED when available, falls back to /dev/urandom.
class STRAYLIGHT_EXPORT EntropySource {
public:
    EntropySource();
    ~EntropySource();

    /// Fill buffer with random bytes.
    straylight::Result<void, std::string> fill(void* buf, size_t len);

    /// Generate a random 64-bit integer.
    straylight::Result<uint64_t, std::string> random_u64();

    /// Run basic health check on the entropy source.
    straylight::Result<void, std::string> health_check();

    /// Whether hardware RNG (RDRAND) is available.
    [[nodiscard]] bool has_hardware_rng() const noexcept { return has_rdrand_; }

private:
    bool has_rdrand_ = false;
    int urandom_fd_ = -1;
};

} // namespace straylight::hw
```

```cpp
// lib/hw/src/entropy.cpp
#include <straylight/hw/entropy.h>
#include <straylight/log.h>

#include <fcntl.h>
#include <unistd.h>
#include <cstring>

#ifdef __x86_64__
#include <immintrin.h>
#include <cpuid.h>
#endif

namespace straylight::hw {

EntropySource::EntropySource() {
#ifdef __x86_64__
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        has_rdrand_ = (ecx >> 30) & 1;
    }
#endif
    urandom_fd_ = ::open("/dev/urandom", O_RDONLY);
    SL_DEBUG("EntropySource: RDRAND={}, urandom_fd={}", has_rdrand_, urandom_fd_);
}

EntropySource::~EntropySource() {
    if (urandom_fd_ >= 0) ::close(urandom_fd_);
}

straylight::Result<void, std::string> EntropySource::fill(void* buf, size_t len) {
#ifdef __x86_64__
    if (has_rdrand_ && len <= 4096) {
        auto* p = static_cast<uint64_t*>(buf);
        size_t remaining = len;
        while (remaining >= 8) {
            unsigned long long val;
            if (_rdrand64_step(&val)) {
                std::memcpy(p, &val, 8);
                p++;
                remaining -= 8;
            } else {
                break;  // Fall through to urandom
            }
        }
        if (remaining == 0) {
            return straylight::Result<void, std::string>::ok();
        }
        // Handle remaining bytes with urandom below
    }
#endif

    if (urandom_fd_ < 0) {
        return straylight::Result<void, std::string>::error("/dev/urandom not available");
    }

    size_t total = 0;
    auto* dst = static_cast<uint8_t*>(buf);
    while (total < len) {
        auto n = ::read(urandom_fd_, dst + total, len - total);
        if (n <= 0) {
            return straylight::Result<void, std::string>::error("read(/dev/urandom) failed");
        }
        total += static_cast<size_t>(n);
    }

    return straylight::Result<void, std::string>::ok();
}

straylight::Result<uint64_t, std::string> EntropySource::random_u64() {
    uint64_t val = 0;
    auto r = fill(&val, sizeof(val));
    if (!r.has_value()) return straylight::Result<uint64_t, std::string>::error(r.error());
    return straylight::Result<uint64_t, std::string>::ok(val);
}

straylight::Result<void, std::string> EntropySource::health_check() {
    // Basic health: generate 256 bytes, verify not all zeros or all ones
    uint8_t buf[256];
    auto r = fill(buf, sizeof(buf));
    if (!r.has_value()) return r;

    int ones = 0;
    for (auto b : buf) {
        for (int i = 0; i < 8; i++) ones += (b >> i) & 1;
    }

    // Monobit test: expect ~1024 ones out of 2048 bits
    // Accept if between 800 and 1248 (very loose bounds)
    if (ones < 800 || ones > 1248) {
        return straylight::Result<void, std::string>::error(
            "Entropy health check failed: monobit test (" + std::to_string(ones) + "/2048)");
    }

    return straylight::Result<void, std::string>::ok();
}

} // namespace straylight::hw
```

- [ ] **Step 5: Update lib/hw/CMakeLists.txt**

```cmake
add_library(straylight-hw SHARED
    src/gpu.cpp
    src/entropy.cpp
)

target_include_directories(straylight-hw
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-hw PUBLIC straylight-common)

set_target_properties(straylight-hw PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-hw PRIVATE straylight_hw_EXPORTS)
```

- [ ] **Step 6: Create tests, build, run**

```cmake
# tests/unit/hw/CMakeLists.txt
add_executable(test_hw_gpu test_gpu.cpp)
target_link_libraries(test_hw_gpu PRIVATE straylight-hw GTest::gtest_main)
gtest_discover_tests(test_hw_gpu)

add_executable(test_hw_entropy test_entropy.cpp)
target_link_libraries(test_hw_entropy PRIVATE straylight-hw GTest::gtest_main)
gtest_discover_tests(test_hw_entropy)
```

Add to `tests/CMakeLists.txt`: `add_subdirectory(unit/hw)`

```bash
cmake --preset dev && cmake --build build/dev
ctest --test-dir build/dev --output-on-failure
# Expected: ALL tests pass across all 4 libraries
```

- [ ] **Step 7: Commit**

```bash
git add lib/hw/ tests/unit/hw/ tests/CMakeLists.txt
git commit -m "feat(hw): add GPU slab allocator and hardware entropy source"
```

---

### Task 11: Final Integration — Verify All Libraries Build and Link

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build/dev
cmake --preset dev
cmake --build build/dev -j$(nproc)
```

- [ ] **Step 2: Run full test suite**

```bash
ctest --test-dir build/dev --output-on-failure
# Expected: All tests pass
```

- [ ] **Step 3: Verify shared library outputs**

```bash
ls -la build/dev/lib/
# Expected:
#   libstraylight-common.so -> libstraylight-common.so.1
#   libstraylight-common.so.1 -> libstraylight-common.so.1.0.0
#   libstraylight-common.so.1.0.0
#   libstraylight-ml.so -> ...
#   libstraylight-net.so -> ...
#   libstraylight-hw.so -> ...
```

- [ ] **Step 4: Verify symbol visibility**

```bash
nm -D build/dev/lib/libstraylight-common.so | grep " T " | head -20
# Expected: Only STRAYLIGHT_EXPORT functions visible (Log::init, Config::load, etc.)
# Internal helpers should NOT appear
```

- [ ] **Step 5: Commit final state**

```bash
git add -A
git commit -m "build: verify all 4 shared libraries build, link, and pass tests"
```
