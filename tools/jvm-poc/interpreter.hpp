#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <vector>

namespace jvmpoc {

struct RuntimePrint {
    uint32_t pc = 0;
    Value value;
};

struct ExecutionTrace {
    std::vector<LocalWrite> localWrites;
    std::vector<RuntimePrint> runtimePrints;
};

ExecutionTrace executeStraightLine(const ClassFile& cls, const MethodInfo& method);

} // namespace jvmpoc
