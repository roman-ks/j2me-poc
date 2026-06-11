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
    // Per-frame opcode histogram. Indexed by raw bytecode op (0x00-0xff).
    // Incremented once per dispatched bytecode in the interpreter loop.
    // Diagnostic — adds ~5ns/step overhead. Reset to zero each frame via
    // resetRuntimeTrace() (default-constructs ExecutionTrace).
    uint32_t opcodeCounts[256] = {};
    // Per-opcode cumulative CPU cycles (CCOUNT delta around each handler).
    // Always populated on ESP32; zero on Linux. Divide by CPU MHz to get µs.
    // Compare with opcodeCounts[op] to find opcodes that cost dramatically
    // more cycles on slow frames vs fast frames — points at the data
    // structure causing PSRAM cache misses.
    uint32_t opcodeCycles[256] = {};
    // CPU cycles spent inside pushJavaFrame across the frame. Accumulated by
    // an OpCycleGuard at the top of pushJavaFrame body. Divide by CPU MHz
    // for µs. If this dominates 0xb7 / 0xb6 totals, frame-push overhead is
    // the bottleneck (arg copy, callStack push, locals init, etc.).
    uint32_t pushFrameCycles = 0;
    // CPU cycles spent executing native method bodies called via invoke*
    // opcodes (0xb6/0xb7/0xb9). Captured by CCOUNT around the actual
    // cachedLeaf / cachedNativeHandler / handleNativeInstanceCall dispatch.
    // Subtract from opcodeCycles[0xb6]+[0xb7]+[0xb9] to get pure dispatch
    // overhead (receiver lookup, callCache, arg pop) vs native body cost.
    uint32_t invokeNativeCycles = 0;
    // CPU cycles spent in rt.heap.find() for getfield (0xb4) receiver
    // lookup. Compare against fieldAccessCycles and opcodeCycles[0xb4]:
    // opcodeCycles[0xb4] - fieldHeapFindCycles - fieldAccessCycles is pure
    // dispatch overhead (cpIdx decode, pop/push).
    uint32_t fieldHeapFindCycles = 0;
    // CPU cycles spent on the fieldIndexCache lookup + obj.fields[slot]
    // read for getfield (0xb4) once the receiver object is found. Isolates
    // the PSRAM cost of HeapObject::fields (std::vector<Value>, default
    // allocator) from the heap.find() cost above.
    uint32_t fieldAccessCycles = 0;
    // Sub-slice of fieldAccessCycles: just the obj.fields[slot] PSRAM read
    // (size() bounds-check + element copy) on the getfield hot path.
    // fieldAccessCycles - fieldVectorReadCycles = the fieldIndexCache.find
    // (second unordered_map) + callCacheKey compute. Tells us whether
    // getfield's cost is the second hash lookup or the field-vector PSRAM
    // access.
    uint32_t fieldVectorReadCycles = 0;
    // Diagnostics for fieldIndexCache (SramAllocator-backed). Populated once
    // per frame in finishProfile. Confirms whether the map that's supposed to
    // stay in internal SRAM actually spilled to PSRAM (SramAllocator silently
    // falls back to ::operator new when SRAM is exhausted), which would explain
    // a getfield-cache find that costs PSRAM-miss cycles instead of SRAM speed.
    uint32_t fieldIndexCacheSize = 0;     // live entry count
    uint32_t fieldIndexCacheBuckets = 0;  // bucket_count() — load-factor view
    // 0 = empty/unknown, 1 = a sampled node is in internal SRAM,
    // 2 = a sampled node is in external PSRAM (spilled). Always 0 on Linux.
    uint8_t fieldIndexCacheNodeMem = 0;
    // Field-slot inline-cache hit/miss across getfield+putfield this frame.
    // hit ÷ (hit+miss) is the L1 hit rate; a low rate means direct-mapped
    // collisions are thrashing (bump kFieldSlotICBits) or warmup dominates.
    uint32_t fieldICHits = 0;
    uint32_t fieldICMisses = 0;
    // Internal-SRAM heap headroom at frame end (ESP32 only; 0 on Linux).
    // Decides whether fieldIndexCache *could* be kept resident: if free and
    // largest-block both comfortably exceed the ~5KB map, the spill is a
    // lazy-allocation timing artifact (fixable by reserving/pre-warming
    // early); if they're tiny, residency is impossible and the per-site IC
    // (one early contiguous block) is the only path.
    uint32_t internalFreeBytes = 0;
    uint32_t internalLargestBlock = 0;
    // CPU cycles spent in rt.primitiveArrays.find()/rt.arrays.find() for
    // array loads (0x2e-0x35: *aload), aggregated across all array-load
    // opcodes since they share loadArrayElement(). Compare against
    // arrayAccessCycles and the summed opcodeCycles for 0x2e-0x35.
    uint32_t arrayHeapFindCycles = 0;
    // CPU cycles spent reading the element out of the resolved vector
    // (CompactArrayHeap or ArrayHeap, both PSRAM-backed) for array loads
    // (0x2e-0x35).
    uint32_t arrayAccessCycles = 0;
    // CPU cycles elapsed inside renderSession. On ESP32 captured via the
    // Xtensa CCOUNT special register (RSR.CCOUNT). Divide by CPU MHz (240 on
    // ESP32-S3 default) to get CPU-µs. Compare against renderSessionUs (wall
    // µs) to detect preemption: if cpuUs ≪ wallUs the loopTask was suspended
    // mid-frame. CCOUNT is 32-bit and wraps every ~17.9s at 240MHz; uint32_t
    // subtraction handles wraparound naturally for sub-second frames.
    // Always zero on the Linux host build.
    uint32_t cpuCycles = 0;
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
