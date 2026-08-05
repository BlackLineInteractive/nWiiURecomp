#pragma once

#include "nwiiu/analyzer/rpx.h"
#include "runtime/cpu_context.h"
#include "runtime/memory.h"

#include <cstdint>
#include <map>
#include <string>

namespace nwii::runtime {
struct ImportTarget {
    std::string module;
    std::string symbol;
};

struct ExecutionImage {
    GuestMemory memory;
    uint32_t entry_point{};
    uint32_t stack_base{};
    uint32_t stack_top{};
    uint32_t sda_base{};
    uint32_t sda2_base{};
    std::map<uint32_t, uint32_t> branch_overrides;
    std::map<uint32_t, ImportTarget> imports;
};

ExecutionImage make_execution_image(const nwiiu::analyzer::RpxImage& rpx);
void initialize_cpu(const ExecutionImage& image, CPUContext& cpu);
} // namespace nwii::runtime
