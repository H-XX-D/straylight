// bin/scheduler/cgroup.cpp
#include "cgroup.h"

#include <fstream>
#include <string>

namespace straylight {

Result<unsigned, SLError> CgroupV2::read_cpu_weight() const {
    auto file_path = path_ / "cpu.weight";
    std::ifstream f(file_path);
    if (!f.is_open()) {
        return Result<unsigned, SLError>::error(
            SLError{SLErrorCode::NotFound,
                    "cannot open " + file_path.string()});
    }

    unsigned weight = 0;
    f >> weight;
    if (f.fail()) {
        return Result<unsigned, SLError>::error(
            SLError{SLErrorCode::ParseError,
                    "failed to parse cpu.weight from " + file_path.string()});
    }

    return Result<unsigned, SLError>::ok(weight);
}

Result<void, SLError> CgroupV2::set_cpu_weight(unsigned weight) const {
    auto file_path = path_ / "cpu.weight";
    std::ofstream f(file_path, std::ios::trunc);
    if (!f.is_open()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied,
                    "cannot write " + file_path.string()});
    }

    f << weight << "\n";
    if (f.fail()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal,
                    "write failed for " + file_path.string()});
    }

    return Result<void, SLError>::ok();
}

Result<void, SLError> CgroupV2::set_memory_max(size_t bytes) const {
    auto file_path = path_ / "memory.max";
    std::ofstream f(file_path, std::ios::trunc);
    if (!f.is_open()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied,
                    "cannot write " + file_path.string()});
    }

    if (bytes == 0) {
        f << "max\n"; // unlimited
    } else {
        f << bytes << "\n";
    }

    if (f.fail()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal,
                    "write failed for " + file_path.string()});
    }

    return Result<void, SLError>::ok();
}

} // namespace straylight
