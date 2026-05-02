#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace jvmpoc {

class JvmHost;
class MidletSession;

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

struct UnknownMethodCall {
    std::string methodLabel;
    uint32_t pc = 0;
    std::string methodName;
    std::vector<Value> args;
    bool nooped = false;
    std::string result;
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

struct DisplaySetCurrent {
    std::string methodLabel;
    uint32_t pc = 0;
    Value display;
    Value displayable;
};

struct CanvasSizeQuery {
    std::string methodLabel;
    uint32_t pc = 0;
    Value canvas;
    std::string methodName;
    int value = 0;
};

struct GraphicsOp {
    std::string methodLabel;
    uint32_t pc = 0;
    std::string op;
};

struct ImageLoad {
    std::string methodLabel;
    uint32_t pc = 0;
    Value image;
    std::string source;
    int width = 0;
    int height = 0;
};

struct ExecutionTrace {
    std::vector<LocalWrite> localWrites;
    std::vector<RuntimePrint> runtimePrints;
    std::vector<UnsupportedStringCall> unsupportedStringCalls;
    std::vector<UnknownMethodCall> unknownMethodCalls;
    std::vector<BranchTrace> branches;
    std::vector<StaticWrite> staticWrites;
    std::vector<ObjectAlloc> objectAllocs;
    std::vector<FieldWrite> fieldWrites;
    std::vector<ArrayAlloc> arrayAllocs;
    std::vector<ArrayWrite> arrayWrites;
    std::vector<GcReport> gcReports;
    std::vector<DisplaySetCurrent> displaySetCurrents;
    std::vector<CanvasSizeQuery> canvasSizeQueries;
    std::vector<GraphicsOp> graphicsOps;
    std::vector<ImageLoad> imageLoads;
    std::vector<std::string> stackSnapshot;
    std::vector<std::string> suspendedTasks;
    std::string currentDisplayableClass;
    std::vector<std::string> currentDisplayableFields;
    bool stepLimitHit = false;
};

std::shared_ptr<MidletSession> createMidletSession(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host = nullptr);
ExecutionTrace startMidletSession(MidletSession& session);
ExecutionTrace renderMidletSession(
    MidletSession& session,
    std::vector<uint16_t>& pixels,
    int width,
    int height);

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method);
ExecutionTrace executeMidlet(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host = nullptr);
ExecutionTrace renderMidletFrame(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host,
    std::vector<uint16_t>& pixels,
    int width,
    int height);

} // namespace jvmpoc
