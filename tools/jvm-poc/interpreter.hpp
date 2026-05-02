#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <string>
#include <vector>

namespace jvmpoc {

struct RuntimePrint {
    std::string methodLabel;
    uint32_t pc = 0;
    Value value;
};

struct UnsupportedStringCall {
    std::string methodLabel;
    uint32_t pc = 0;
    std::string methodName;
    Value receiver;
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

struct ObjectAlloc {
    std::string methodLabel;
    uint32_t pc = 0;
    Value ref;
    std::string className;
};

struct FieldWrite {
    std::string methodLabel;
    uint32_t pc = 0;
    Value ref;
    std::string fieldName;
    Value value;
};

struct ArrayAlloc {
    std::string methodLabel;
    uint32_t pc = 0;
    Value ref;
    size_t length = 0;
};

struct ArrayWrite {
    std::string methodLabel;
    uint32_t pc = 0;
    Value ref;
    Value index;
    Value value;
};

struct GcReport {
    std::string when;
    std::vector<std::string> roots;
    std::vector<Value> unreachableObjects;
    std::vector<Value> unreachableArrays;
    std::vector<Value> unreachableStrings;
    std::vector<Value> freedObjects;
    std::vector<Value> freedArrays;
    std::vector<Value> freedStrings;
};

struct ExecutionTrace {
    std::vector<LocalWrite> localWrites;
    std::vector<RuntimePrint> runtimePrints;
    std::vector<UnsupportedStringCall> unsupportedStringCalls;
    std::vector<BranchTrace> branches;
    std::vector<StaticWrite> staticWrites;
    std::vector<ObjectAlloc> objectAllocs;
    std::vector<FieldWrite> fieldWrites;
    std::vector<ArrayAlloc> arrayAllocs;
    std::vector<ArrayWrite> arrayWrites;
    std::vector<GcReport> gcReports;
    bool stepLimitHit = false;
};

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method);

} // namespace jvmpoc
