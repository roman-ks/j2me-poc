#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <vector>

namespace jvmpoc {

struct RuntimePrint {
    uint32_t pc = 0;
    Value value;
};

struct BranchTrace {
    uint32_t pc = 0;
    std::string condition;
    bool known = false;
    bool taken = false;
    uint32_t targetPc = 0;
};

struct ExecutionTrace {
    std::vector<LocalWrite> localWrites;
    std::vector<RuntimePrint> runtimePrints;
    std::vector<BranchTrace> branches;
    bool stepLimitHit = false;
};

ExecutionTrace executeStraightLine(const ClassFile& cls, const MethodInfo& method);

} // namespace jvmpoc
