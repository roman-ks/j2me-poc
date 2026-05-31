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
    uint64_t durationMillis = 0;
    std::vector<std::string> roots;
    std::vector<Value> unreachableObjects;
    std::vector<Value> unreachableArrays;
    std::vector<Value> unreachableStrings;
    std::vector<Value> freedObjects;
    std::vector<Value> freedArrays;
    std::vector<Value> freedStrings;
};

struct ResourceRead {
    std::string path;
    std::string resolvedPath;
    bool ok = false;
    size_t bytes = 0;
    uint64_t durationMillis = 0;
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

struct FrameProfile {
    uint32_t renderSessionUs = 0;
    uint32_t inputUs = 0;
    uint32_t tasksUs = 0;
    uint32_t taskInvokeUs = 0;
    uint32_t taskYieldUnwindUs = 0;
    uint32_t taskCatchUs = 0;
    uint32_t taskSuspendSaveUs = 0;
    uint32_t taskClearUs = 0;
    uint32_t displayLookupUs = 0;
    uint32_t displayTraceUs = 0;
    uint32_t paintLookupUs = 0;
    uint32_t paintUs = 0;
    uint32_t suspendedTraceUs = 0;
    uint32_t steps = 0;
    uint16_t inputEvents = 0;
    uint16_t taskRuns = 0;
    uint16_t taskSkippedSleeping = 0;
    uint16_t taskSleepYields = 0;
    uint32_t taskSleepRequestedMs = 0;
    bool repaintRequested = false;
    bool paintCalled = false;
    bool displayableFound = false;
};

struct MethodProfile {
    std::string methodLabel;
    uint32_t calls = 0;
    uint64_t totalUs = 0;
    uint32_t maxUs = 0;
};

struct UncaughtExceptionTrace {
    std::string threadLabel;
    std::string methodLabel;
    uint32_t pc = 0;
    std::string exceptionClass;
};

struct CaughtExceptionTrace {
    std::string throwMethodLabel;
    uint32_t throwPc = 0;
    std::string catchMethodLabel;
    uint32_t catchPc = 0;
    std::string exceptionClass;
};

struct ThreadDeathTrace {
    std::string threadLabel;
    std::string exceptionClass;
};

struct ExecutionTrace {
    bool recording = true;
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
    std::vector<ResourceRead> resourceReads;
    std::vector<std::string> stackSnapshot;
    std::vector<std::string> suspendedTasks;
    std::vector<UncaughtExceptionTrace> uncaughtExceptions;
    std::vector<CaughtExceptionTrace> caughtExceptions;
    std::vector<ThreadDeathTrace> threadDeaths;
    std::vector<MethodProfile> taskMethodProfiles;
    std::vector<MethodProfile> taskNativeProfiles;
    std::string currentDisplayableClass;
    FrameProfile frameProfile;
    bool stepLimitHit = false;
    // True when paint() completed this render tick — signals that the
    // framebuffer is ready to display. False on step-limit mid-render yields.
    bool framePresented = false;
};

std::shared_ptr<MidletSession> createMidletSession(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host = nullptr);
ExecutionTrace startMidletSession(MidletSession& session);
void setTraceRecording(MidletSession& session, bool enabled);
ExecutionTrace renderMidletSession(
    MidletSession& session,
    uint16_t* pixels,
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
    uint16_t* pixels,
    int width,
    int height);

} // namespace jvmpoc
