#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <vector>

namespace jvmpoc {

struct RuntimePrint {
    std::string methodLabel;
    uint32_t pc = 0;
    Value value;
};

struct BranchTrace {
    std::string methodLabel;
    uint32_t pc = 0;
    std::string condition;
    bool known = false;
    bool taken = false;
    uint32_t targetPc = 0;
};

struct StaticWrite {
    std::string methodLabel;
    uint32_t pc = 0;
    std::string fieldName;
    Value value;
};

struct ExecutionTrace {
    std::vector<LocalWrite> localWrites;
    std::vector<RuntimePrint> runtimePrints;
    std::vector<BranchTrace> branches;
    std::vector<StaticWrite> staticWrites;
    bool stepLimitHit = false;
};

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method);

} // namespace jvmpoc
