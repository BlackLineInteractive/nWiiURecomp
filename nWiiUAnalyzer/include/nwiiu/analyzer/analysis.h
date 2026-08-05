#pragma once
#include "nwiiu/analyzer/rpx.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nwiiu::analyzer {
struct DynamicTransfer {
    uint32_t address{};
    std::string kind;
};

struct Unresolved {
    uint32_t address{};
    std::string category;
    std::string reason;
};

struct BasicBlock {
    uint32_t start{};
    uint32_t end{};
    uint32_t instruction_count{};
};

struct Function {
    uint32_t start{};
    uint32_t end{};
    uint32_t instruction_count{};
    std::vector<BasicBlock> basic_blocks;
    std::set<uint32_t> callers;
    std::set<uint32_t> callees;
    std::set<uint32_t> jump_table_targets;
    std::set<std::string> discovery_reasons;
    std::vector<DynamicTransfer> dynamic_transfers;
};

struct Analysis {
    std::map<uint32_t, Function> functions;
    std::vector<Unresolved> unresolved;
};

Analysis analyze(const RpxImage& image);
}
