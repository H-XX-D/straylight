// bin/scheduler/cgroup.h
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>

namespace straylight {

/// Read/write cgroup v2 knobs for a single cgroup directory.
class CgroupV2 {
public:
    explicit CgroupV2(std::filesystem::path path) : path_(std::move(path)) {}

    Result<unsigned, SLError> read_cpu_weight() const;
    Result<void, SLError> set_cpu_weight(unsigned weight) const;
    Result<void, SLError> set_memory_max(size_t bytes) const;

private:
    std::filesystem::path path_;
};

} // namespace straylight
