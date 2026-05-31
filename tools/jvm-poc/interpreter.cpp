#include "interpreter.hpp"

#include "bytecode.hpp"
#include "jvm_host.hpp"
#include "method_resolution.hpp"
#include "native_methods.hpp"
#include "sram_allocator.hpp"
#include "j2me_port/J2MECompat.hpp"

#ifdef ESP32_BUILD
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include <iostream>
#include <core/log.h>

namespace jvmpoc {
namespace {

constexpr size_t kMaxSteps = 100000;
constexpr size_t kMaxCallDepth = 64;
constexpr uint16_t kAccNative = 0x0100;
constexpr const char* kClassHandlePrefix = "class:";

uint32_t branchTarget(size_t pc, int32_t offset) {
    return static_cast<uint32_t>(static_cast<int32_t>(pc) + offset);
}

template<typename To, typename From>
To bitCast(From f) {
    static_assert(sizeof(To) == sizeof(From), "bitCast size mismatch");
    To t;
    std::memcpy(&t, &f, sizeof(t));
    return t;
}

inline uint32_t nowUs() {
#ifdef ESP32_BUILD
    return static_cast<uint32_t>(esp_timer_get_time());
#else
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

std::string compareText(const Value& lhs, const char* op, const Value& rhs) {
    return lhs.asText() + " " + op + " " + rhs.asText();
}

Value intBinaryOp(const Value& lhs, const Value& rhs, const char* op, uint8_t opcode) {
    std::optional<int> left = parseIntValue(lhs);
    std::optional<int> right = parseIntValue(rhs);
    if (left && right) {
        switch (opcode) {
            case 0x60: return Value::ofInt(*left + *right);
            case 0x64: return Value::ofInt(*left - *right);
            case 0x68: return Value::ofInt(*left * *right);
            case 0x6c:
                if (*right == 0) return Value::named("<divide-by-zero>");
                return Value::ofInt(*left / *right);
            default:
                break;
        }
    }
    return Value::named("(" + lhs.asText() + " " + op + " " + rhs.asText() + ")");
}

Value longBinaryOp(const Value& lhs, const Value& rhs, const char* op, uint8_t opcode) {
    std::optional<long long> left = parseLongValue(lhs);
    std::optional<long long> right = parseLongValue(rhs);
    if (left && right) {
        switch (opcode) {
            case 0x61: return Value::ofLong(*left + *right);
            case 0x65: return Value::ofLong(*left - *right);
            case 0x69: return Value::ofLong(*left * *right);
            case 0x6d:
                if (*right == 0) return Value::named("<divide-by-zero>");
                return Value::ofLong(*left / *right);
            default:
                break;
        }
    }
    return Value::named("(" + lhs.asText() + " " + op + " " + rhs.asText() + ")");
}

bool compareInts(int lhs, int rhs, uint8_t op) {
    switch (op) {
        case 0x9f: return lhs == rhs;
        case 0xa0: return lhs != rhs;
        case 0xa1: return lhs < rhs;
        case 0xa2: return lhs >= rhs;
        case 0xa3: return lhs > rhs;
        case 0xa4: return lhs <= rhs;
        default: return false;
    }
}

bool compareIntWithZero(int value, uint8_t op) {
    switch (op) {
        case 0x99: return value == 0;
        case 0x9a: return value != 0;
        case 0x9b: return value < 0;
        case 0x9c: return value >= 0;
        case 0x9d: return value > 0;
        case 0x9e: return value <= 0;
        default: return false;
    }
}

const char* compareZeroOpText(uint8_t op) {
    switch (op) {
        case 0x99: return "==";
        case 0x9a: return "!=";
        case 0x9b: return "<";
        case 0x9c: return ">=";
        case 0x9d: return ">";
        case 0x9e: return "<=";
        default: return "?";
    }
}

const char* compareOpText(uint8_t op) {
    switch (op) {
        case 0x9f: return "==";
        case 0xa0: return "!=";
        case 0xa1: return "<";
        case 0xa2: return ">=";
        case 0xa3: return ">";
        case 0xa4: return "<=";
        default: return "?";
    }
}

std::string methodLabel(const ClassFile& cls, const MethodInfo& method) {
    return cls.thisClass + "." + method.name + method.descriptor;
}

std::vector<size_t> argumentSlotWidths(const std::string& descriptor) {
    std::vector<size_t> widths;
    size_t pos = 0;
    if (descriptor.empty() || descriptor[pos++] != '(') {
        return widths;
    }

    while (pos < descriptor.size() && descriptor[pos] != ')') {
        char c = descriptor[pos++];
        if (c == 'J' || c == 'D') {
            widths.push_back(2);
        } else if (c == 'L') {
            while (pos < descriptor.size() && descriptor[pos++] != ';') {
            }
            widths.push_back(1);
        } else if (c == '[') {
            while (pos < descriptor.size() && descriptor[pos] == '[') {
                ++pos;
            }
            if (pos < descriptor.size() && descriptor[pos] == 'L') {
                while (pos < descriptor.size() && descriptor[pos++] != ';') {
                }
            } else if (pos < descriptor.size()) {
                ++pos;
            }
            widths.push_back(1);
        } else {
            widths.push_back(1);
        }
    }

    return widths;
}

bool returnsValue(const std::string& descriptor) {
    size_t close = descriptor.find(')');
    return close != std::string::npos && close + 1 < descriptor.size() && descriptor[close + 1] != 'V';
}

std::string callName(const MethodRef& ref) {
    return ref.className + "." + ref.name + ref.descriptor;
}

MethodRef methodRefForOwner(const ClassFile& owner, const MethodRef& ref) {
    return MethodRef{owner.thisClass, ref.name, ref.descriptor};
}

inline uint64_t callCacheKey(const ClassFile* cls, uint16_t cpIndex) {
    return (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cls)) << 16) | cpIndex;
}

struct ResolvedCallEntry {
    uint8_t argSlots;
    bool isNative;
    std::string runtimeClass; // static/special: stored ref.className; virtual: last runtime type
    const ClassFile* runtimeClassPtr = nullptr; // hot compare: pointer equality instead of string compare
    const ClassFile* targetClass;
    const MethodInfo* method;
    // Option N(v1): resolved class-level native handler. Bypasses the
    // string-compare cascade in handleNativeStaticCall/handleNativeInstanceCall.
    // Set only for native calls whose className maps cleanly to a single
    // handler; nullptr otherwise (slow cascade still runs).
    NativeHandler nativeHandler = nullptr;
};

struct HeapObject {
    std::string className;
    const ClassFile* cls = nullptr; // cached class pointer for fast virtual dispatch (item K)
    std::vector<Value> fields;      // indexed by slot from fieldSlotCache (item D)
};

using Heap = std::unordered_map<uint32_t, HeapObject>;
using ArrayHeap = std::unordered_map<uint32_t, std::vector<Value>>;
using CompactArrayHeap = std::unordered_map<uint32_t, std::vector<int32_t>>;
using StringHeap = std::unordered_map<uint32_t, std::string>;
using ImageHeap = std::unordered_map<uint32_t, port::Image>;
using ResourceImageCache = std::map<std::string, uint32_t>;

struct RuntimeFrame {
    std::string label;
    const ClassFile* cls = nullptr;
    const MethodInfo* method = nullptr;
    size_t pc = 0;
    // Trampoline support: pc of the invoke bytecode that started the pending
    // Java call from this frame. SIZE_MAX = no pending call. Read by the entry
    // exception guard in resumeCurrentMethod to know where to look up handlers
    // when a callee throws and propagates back here.
    size_t lastCallPc = SIZE_MAX;
    Frame frame;
    // Populated only while this RuntimeFrame lives in ThreadTask::suspendedFrames.
    // Holds a copy of the [locals... stack...] slot range that was in the arena
    // before yield; restored back into the arena on resume.
    std::vector<Value> suspendedSlots;
};

// Shared bump-allocated frame slot buffer. Replaces the per-frame malloc that
// std::vector<Value, SramAllocator> used to do. Sized once at Runtime
// construction; never grows. Slots are default-constructed Values (kNone);
// frame allocation just hands out a Value* pointing into the buffer.
//
// Allocation point: callStack.back().frame.stackEnd() — i.e., the actual
// operand-stack top of the calling frame (not stackBase + maxStack). This
// transparently handles cases where the interpreter pushes more than the
// .class-declared max_stack.
//
// Pop is implicit: the next allocation reads stackEnd() of the new top frame.
struct FrameArena {
    std::vector<Value, SramAllocator<Value>> slots;
    Value* begin() { return slots.data(); }
    Value* end() { return slots.data() + slots.size(); }
    const Value* begin() const { return slots.data(); }
    const Value* end() const { return slots.data() + slots.size(); }
};

constexpr size_t kFrameArenaSlots = 2730; // ≈ 32 KB at sizeof(Value)=12 on ESP32

struct ThreadTask {
    const ClassFile* cls = nullptr;
    const MethodInfo* method = nullptr;
    Value receiver = Value::named("0");
    std::vector<RuntimeFrame, SramAllocator<RuntimeFrame>> suspendedFrames;
    uint32_t wakeAtMillis = 0;
    bool finished = false;
    std::optional<Value> pendingException;
};

struct MethodProfileAccumulator {
    const ClassFile* cls = nullptr;
    const MethodInfo* method = nullptr;
    uint32_t calls = 0;
    uint64_t totalUs = 0;
    uint32_t maxUs = 0;
};

struct NamedProfileAccumulator {
    std::string label;
    uint32_t calls = 0;
    uint64_t totalUs = 0;
    uint32_t maxUs = 0;
};

struct Runtime {
    Runtime() {
        frameArena.slots.resize(kFrameArenaSlots);
    }

    FrameArena frameArena;
    const JvmHost* host = nullptr;
    MidletSession* session = nullptr;
    ThreadTask* currentTask = nullptr;
    std::unordered_map<std::string, Value> staticFields;
    Heap heap;
    ArrayHeap arrays;
    CompactArrayHeap primitiveArrays;
    StringHeap strings;
    ImageHeap images;
    ResourceImageCache resourceImages;
    std::map<std::string, uint32_t> internedStrings;
    // Cache (cls*, cpIdx) → "ClassName.fieldName" key for staticFields lookups.
    std::unordered_map<uint64_t, std::string,
        std::hash<uint64_t>, std::equal_to<uint64_t>,
        SramAllocator<std::pair<const uint64_t, std::string>>> staticKeyCache;
    uint32_t nextObjectId = 1;
    uint32_t nextArrayId = 1;
    uint32_t nextImageId = 1;
    std::vector<uint32_t> freeObjectIds;
    std::vector<uint32_t> freeArrayIds;
    std::vector<RuntimeFrame, SramAllocator<RuntimeFrame>> callStack;
    // Reusable arg buffer — avoids one SRAM malloc per method call. Safe because
    // initializeFrameArgs moves elements out of callArgsBuf before the interpreter
    // loop runs (which is the only code that could re-use callArgsBuf recursively).
    // Uses default allocator: on ESP32, allocations <=4KB go to SRAM by default.
    std::vector<Value> callArgsBuf;
    std::unordered_map<uint64_t, ResolvedCallEntry,
        std::hash<uint64_t>, std::equal_to<uint64_t>,
        SramAllocator<std::pair<const uint64_t, ResolvedCallEntry>>> callCache;
    // Cache (cls*, cpIdx) → pointer into cls.cp UTF8 entry for field name.
    // ClassFile objects are stable for the session so the pointer is safe.
    std::unordered_map<uint64_t, const std::string*,
        std::hash<uint64_t>, std::equal_to<uint64_t>,
        SramAllocator<std::pair<const uint64_t, const std::string*>>> fieldNameCache;
    // Cache (obj.cls*, cpIdx) → slot index for hot-path getfield/putfield (item D).
    // Key = callCacheKey(obj.cls, cpIdx); SRAM-allocated so the lookup stays off PSRAM.
    std::unordered_map<uint64_t, uint16_t,
        std::hash<uint64_t>, std::equal_to<uint64_t>,
        SramAllocator<std::pair<const uint64_t, uint16_t>>> fieldIndexCache;
    // Build-time maps: cls* → {fieldKey → slot} and cls* → total slot count.
    // fieldKey is "name|descriptor" composite; see fieldSlotKey(). The same
    // map also holds bare-name entries that point to the first slot assigned
    // for that name, so native handlers that call ctx.readField(name) without
    // a descriptor still resolve correctly when there is exactly one field
    // with that name (true for every class accessed by current natives).
    std::unordered_map<const ClassFile*, std::unordered_map<std::string, uint16_t>> fieldSlotCache;
    std::unordered_map<const ClassFile*, uint16_t> fieldSlotCount;
    // Cache (cls*, cpIdx) → "name|descriptor" composite. Strings owned here.
    std::unordered_map<uint64_t, std::string,
        std::hash<uint64_t>, std::equal_to<uint64_t>,
        SramAllocator<std::pair<const uint64_t, std::string>>> fieldKeyCache;
    Value displayRef = Value::named("display#1");
    Value currentDisplayable = Value::named("0");
    uint16_t* graphicsFramebuffer = nullptr;
    int graphicsWidth = 0;
    int graphicsHeight = 0;
    int graphicsColorRgb = 0x000000;
    // Persistent main-framebuffer Canvas. Shared across all executeMethod() depths
    // so translate/clip state set in paint() persists into Java sub-method calls.
    std::optional<port::Canvas> mainFbCanvas;
    // Set by flushGraphics() to distinguish a committed GameCanvas frame from a
    // spurious repaint() call (e.g. from a timer thread mid-render). Cleared after
    // each paint() call in renderSession.
    bool gameCanvasFlushCommitted = false;
    ExecutionTrace trace;
    std::vector<MethodProfileAccumulator, SramAllocator<MethodProfileAccumulator>> taskMethodProfiles;
    std::vector<NamedProfileAccumulator, SramAllocator<NamedProfileAccumulator>> taskNativeProfiles;
    std::optional<Value> pendingException;
    std::string pendingExceptionMethodLabel;
    uint32_t pendingExceptionPc = 0;
    uint32_t pendingYieldRethrowStartUs = 0;
    bool yieldRequested = false;
    uint32_t yieldMillis = 0;
    bool stepLimitYieldEnabled = false;
    size_t steps = 0;
    bool repaintRequested = true;
    std::unordered_set<std::string> initializedClasses;
};

// Resolve field name from CP without copying strings. Returns pointer into cls.cp (stable).
// Falls back to resolveFieldRef().name (copies once) if CP layout is unexpected.
inline const std::string& resolveFieldName(Runtime& rt, const ClassFile& cls, uint16_t cpIdx) {
    const uint64_t fkey = callCacheKey(&cls, cpIdx);
    auto fcit = rt.fieldNameCache.find(fkey);
    if (fcit != rt.fieldNameCache.end()) {
        return *fcit->second;
    }
    // Cache miss: navigate CP directly to avoid 3 string copies from resolveFieldRef.
    const auto& cp = cls.cp;
    if (cpIdx > 0 && cpIdx < cp.size() && cp[cpIdx].tag == CpFieldref) {
        const uint16_t natIdx = cp[cpIdx].b;
        if (natIdx > 0 && natIdx < cp.size() && cp[natIdx].tag == CpNameAndType) {
            const uint16_t nameIdx = cp[natIdx].a;
            if (nameIdx > 0 && nameIdx < cp.size() && cp[nameIdx].tag == CpUtf8) {
                const std::string* ptr = &cp[nameIdx].utf8;
                rt.fieldNameCache[fkey] = ptr;
                return *ptr;
            }
        }
    }
    // Fallback (should not happen in valid bytecode): resolve the slow way.
    static thread_local std::string fallback;
    fallback = resolveFieldRef(cls, cpIdx).name;
    rt.fieldNameCache[fkey] = &fallback;
    return fallback;
}

// Composite "name|descriptor" key for the slot map. Required to disambiguate
// JVM-legal overloaded instance fields (same name, different descriptor) — e.g.
// Sonic's MainCanvasDraw has both `long a` and `boolean a`. Java forbids this
// at source level but the bytecode is valid. Using name alone would collide
// both onto slot 0 and corrupt every read of the boolean.
inline std::string fieldSlotKey(const std::string& name, const std::string& descriptor) {
    std::string key;
    key.reserve(name.size() + 1 + descriptor.size());
    key.append(name);
    key.push_back('|');
    key.append(descriptor);
    return key;
}

// Resolve full name+descriptor composite for a Fieldref CP entry. Cached per
// (cls, cpIdx). Returns pointer into a thread-local cache string owned by
// fieldKeyCache (stored as std::string by value to keep the lifetime).
inline const std::string& resolveFieldKey(Runtime& rt, const ClassFile& cls, uint16_t cpIdx) {
    const uint64_t fkey = callCacheKey(&cls, cpIdx);
    auto it = rt.fieldKeyCache.find(fkey);
    if (it != rt.fieldKeyCache.end()) {
        return it->second;
    }
    const auto& cp = cls.cp;
    std::string name, descriptor;
    if (cpIdx > 0 && cpIdx < cp.size() && cp[cpIdx].tag == CpFieldref) {
        const uint16_t natIdx = cp[cpIdx].b;
        if (natIdx > 0 && natIdx < cp.size() && cp[natIdx].tag == CpNameAndType) {
            const uint16_t nameIdx = cp[natIdx].a;
            const uint16_t descIdx = cp[natIdx].b;
            if (nameIdx > 0 && nameIdx < cp.size() && cp[nameIdx].tag == CpUtf8) {
                name = cp[nameIdx].utf8;
            }
            if (descIdx > 0 && descIdx < cp.size() && cp[descIdx].tag == CpUtf8) {
                descriptor = cp[descIdx].utf8;
            }
        }
    }
    if (name.empty()) {
        const FieldRef ref = resolveFieldRef(cls, cpIdx);
        name = ref.name;
        descriptor = ref.descriptor;
    }
    return rt.fieldKeyCache.emplace(fkey, fieldSlotKey(name, descriptor)).first->second;
}

// Assign stable slot indices to instance fields of cls (and its superclass chain).
// Superclass fields get lower indices so the layout is consistent across subclasses.
// Returns total slot count (= required size of HeapObject::fields for instances of cls).
uint16_t buildFieldSlots(Runtime& rt, const std::vector<ClassFile>& classes, const ClassFile& cls) {
    auto countIt = rt.fieldSlotCount.find(&cls);
    if (countIt != rt.fieldSlotCount.end()) return countIt->second;

    uint16_t nextSlot = 0;
    if (!cls.superClass.empty()) {
        const ClassFile* superCls = findClass(classes, cls.superClass);
        if (superCls != nullptr) {
            nextSlot = buildFieldSlots(rt, classes, *superCls);
            // Inherit parent name→slot entries.
            auto& mySlots = rt.fieldSlotCache[&cls];
            const auto& superSlots = rt.fieldSlotCache[superCls];
            mySlots.insert(superSlots.begin(), superSlots.end());
        }
    }

    auto& mySlots = rt.fieldSlotCache[&cls];
    for (const FieldInfo& f : cls.fields) {
        if (f.access & 0x0008) continue; // static — not stored on heap object
        const std::string key = fieldSlotKey(f.name, f.descriptor);
        if (mySlots.count(key) != 0) continue;
        const uint16_t slot = nextSlot++;
        mySlots[key] = slot;
        // Bare-name fallback for ctx.readField(name) callers that don't know
        // the descriptor. First-wins when a name is overloaded; native
        // handlers only read uniquely-named fields, so this is safe.
        if (mySlots.count(f.name) == 0) {
            mySlots[f.name] = slot;
        }
    }
    rt.fieldSlotCount[&cls] = nextSlot;
    return nextSlot;
}

} // namespace

class MidletSession {
public:
    MidletSession(const std::vector<ClassFile>& classes, std::string className, const JvmHost* host)
        : classes_(&classes), className_(std::move(className)) {
        runtime_.host = host;
        runtime_.session = this;
        runtime_.callStack.reserve(kMaxCallDepth + 1);
    }

    const std::vector<ClassFile>& classes() const { return *classes_; }
    const std::string& className() const { return className_; }
    Runtime& runtime() { return runtime_; }
    const Runtime& runtime() const { return runtime_; }
    Value& midletRef() { return midlet_; }
    bool started() const { return started_; }
    void setStarted(bool started) { started_ = started; }
    std::vector<ThreadTask>& tasks() { return tasks_; }
    const std::vector<ThreadTask>& tasks() const { return tasks_; }

private:
    const std::vector<ClassFile>* classes_ = nullptr;
    std::string className_;
    Runtime runtime_;
    Value midlet_ = Value::named("0");
    bool started_ = false;
    std::vector<ThreadTask> tasks_;
};

struct YieldThreadSleep {
    uint32_t millis = 0;
};

namespace {

void captureStackSnapshot(ExecutionTrace& trace, const std::vector<RuntimeFrame, SramAllocator<RuntimeFrame>>& callStack) {
    trace.stackSnapshot.clear();
    for (auto it = callStack.rbegin(); it != callStack.rend(); ++it) {
        trace.stackSnapshot.push_back(it->label + " pc=" + std::to_string(it->pc));
    }
}

void captureSuspendedTasks(ExecutionTrace& trace, const MidletSession& session) {
    trace.suspendedTasks.clear();
    for (const ThreadTask& task : session.tasks()) {
        if (task.finished) {
            continue;
        }
        std::string state = task.method != nullptr
            ? task.cls->thisClass + "." + task.method->name + task.method->descriptor
            : std::string("<unknown-task>");
        state += " wake=" + std::to_string(task.wakeAtMillis);
        if (!task.suspendedFrames.empty()) {
            const RuntimeFrame& top = task.suspendedFrames.back();
            state += " suspended=" + top.label + " pc=" + std::to_string(top.pc);
        }
        trace.suspendedTasks.push_back(std::move(state));
    }
}

Value objectRef(uint32_t id) {
    return Value::ofInt(Value::kHandleObjTag | static_cast<int32_t>(id & 0xFFFFFF));
}

Value arrayRef(uint32_t id) {
    return Value::ofInt(Value::kHandleArrTag | static_cast<int32_t>(id & 0xFFFFFF));
}

std::optional<std::string> parseTextHandle(const Value& value, const std::string& prefix) {
    const std::string text = value.asText();
    if (text.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    return text.substr(prefix.size());
}

uint32_t allocateHeapObjectId(Runtime& rt, const std::string& className) {
    uint32_t id = 0;
    if (!rt.freeObjectIds.empty()) {
        id = rt.freeObjectIds.back();
        rt.freeObjectIds.pop_back();
    } else {
        id = rt.nextObjectId++;
    }
    rt.heap[id] = HeapObject{className, nullptr, {}};
    return id;
}

Value allocateObject(Runtime& rt, const std::vector<ClassFile>& classes, const std::string& label, uint32_t pc, const std::string& className) {
    uint32_t id = allocateHeapObjectId(rt, className);
    HeapObject& obj = rt.heap[id];
    obj.cls = findClass(classes, className);
    if (obj.cls != nullptr) {
        // Pre-size fields vector to the stable slot count for this class.
        const uint16_t slotCount = buildFieldSlots(rt, classes, *obj.cls);
        obj.fields.resize(slotCount);
    }
    Value ref = objectRef(id);
    if (rt.trace.recording) rt.trace.objectAllocs.push_back(ObjectAlloc{label, pc, ref, className});
    return ref;
}

Value allocateArray(Runtime& rt, const std::string& label, uint32_t pc, size_t length) {
    uint32_t id = 0;
    if (!rt.freeArrayIds.empty()) {
        id = rt.freeArrayIds.back();
        rt.freeArrayIds.pop_back();
    } else {
        id = rt.nextArrayId++;
    }
    rt.arrays[id] = std::vector<Value>(length, Value::ofInt(0));
    Value ref = arrayRef(id);
    if (rt.trace.recording) rt.trace.arrayAllocs.push_back(ArrayAlloc{label, pc, ref, length});
    return ref;
}

Value allocatePrimitiveArray(Runtime& rt, const std::string& label, uint32_t pc, size_t length) {
    uint32_t id = 0;
    if (!rt.freeArrayIds.empty()) {
        id = rt.freeArrayIds.back();
        rt.freeArrayIds.pop_back();
    } else {
        id = rt.nextArrayId++;
    }
    rt.primitiveArrays[id] = std::vector<int32_t>(length, 0);
    Value ref = arrayRef(id);
    if (rt.trace.recording) rt.trace.arrayAllocs.push_back(ArrayAlloc{label, pc, ref, length});
    return ref;
}

std::optional<uint32_t> objectId(const Value& value);
std::optional<uint32_t> arrayId(const Value& value);

bool writeFieldValue(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const Value& object,
    const std::string& fieldName,
    Value value) {
    std::optional<uint32_t> id = objectId(object);
    if (!id.has_value()) {
        return false;
    }
    auto objectIt = rt.heap.find(*id);
    if (objectIt == rt.heap.end() || objectIt->second.cls == nullptr) {
        return false;
    }

    HeapObject& obj = objectIt->second;
    buildFieldSlots(rt, classes, *obj.cls);
    auto slotCacheIt = rt.fieldSlotCache.find(obj.cls);
    if (slotCacheIt == rt.fieldSlotCache.end()) {
        return false;
    }
    auto nameIt = slotCacheIt->second.find(fieldName);
    if (nameIt == slotCacheIt->second.end()) {
        return false;
    }

    const uint16_t slot = nameIt->second;
    if (slot >= obj.fields.size()) {
        obj.fields.resize(slot + 1);
    }
    obj.fields[slot] = std::move(value);
    return true;
}

Value allocateByteArrayInputStream(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const std::string& label,
    uint32_t pc,
    const std::vector<uint8_t>& data) {
    Value buffer = allocatePrimitiveArray(rt, label, pc, data.size());
    std::optional<uint32_t> bufferId = arrayId(buffer);
    if (bufferId.has_value()) {
        auto primIt = rt.primitiveArrays.find(*bufferId);
        if (primIt != rt.primitiveArrays.end()) {
            for (size_t i = 0; i < data.size(); ++i) {
                primIt->second[i] = static_cast<int32_t>(static_cast<int8_t>(data[i]));
            }
        }
    }

    Value stream = allocateObject(rt, classes, label, pc, "java/io/ByteArrayInputStream");
    writeFieldValue(rt, classes, stream, "buf", buffer);
    writeFieldValue(rt, classes, stream, "pos", Value::ofInt(0));
    writeFieldValue(rt, classes, stream, "mark", Value::ofInt(0));
    writeFieldValue(rt, classes, stream, "count", Value::ofInt(static_cast<int32_t>(data.size())));
    return stream;
}

Value allocateMultiArray(
    Runtime& rt,
    const std::string& label,
    uint32_t pc,
    const std::vector<int>& counts,
    size_t depth) {
    Value ref = allocateArray(rt, label, pc, static_cast<size_t>(counts[depth]));
    if (depth + 1 >= counts.size()) {
        return ref;
    }

    std::optional<uint32_t> id = arrayId(ref);
    if (!id.has_value()) {
        return ref;
    }

    std::vector<Value>& elements = rt.arrays[*id];
    for (Value& element : elements) {
        element = allocateMultiArray(rt, label, pc, counts, depth + 1);
    }
    return ref;
}

std::optional<std::string> runtimeString(const Runtime& rt, const Value& value) {
    std::optional<uint32_t> id = objectId(value);
    if (!id.has_value()) {
        return std::nullopt;
    }
    auto stringIt = rt.strings.find(*id);
    if (stringIt == rt.strings.end()) {
        return std::nullopt;
    }
    return stringIt->second;
}

Value loadArrayElement(Runtime& rt, const Value& arrayValue, const Value& indexValue) {
    std::optional<uint32_t> id = arrayId(arrayValue);
    std::optional<int> index = parseIntValue(indexValue);
    if (id.has_value() && index.has_value() && *index >= 0) {
        const size_t idx = static_cast<size_t>(*index);
        auto primIt = rt.primitiveArrays.find(*id);
        if (primIt != rt.primitiveArrays.end()) {
            return idx < primIt->second.size() ? Value::ofInt(primIt->second[idx]) : Value::ofInt(0);
        }
        auto arrayIt = rt.arrays.find(*id);
        if (arrayIt != rt.arrays.end() && idx < arrayIt->second.size()) {
            return arrayIt->second[idx];
        }
    }
    return Value::ofInt(0);
}

Value normalizeByteValue(const Value& value) {
    std::optional<int> parsed = parseIntValue(value);
    if (!parsed.has_value()) {
        return value;
    }
    return Value::ofInt(static_cast<int>(static_cast<int8_t>(*parsed)));
}

void storeArrayElement(
    Runtime& rt,
    const std::string& label,
    uint32_t pc,
    const Value& arrayValue,
    const Value& indexValue,
    const Value& rawValue,
    bool normalizeByte) {
    Value value = normalizeByte ? normalizeByteValue(rawValue) : rawValue;
    std::optional<uint32_t> id = arrayId(arrayValue);
    std::optional<int> index = parseIntValue(indexValue);
    if (id.has_value() && index.has_value() && *index >= 0) {
        const size_t idx = static_cast<size_t>(*index);
        auto primIt = rt.primitiveArrays.find(*id);
        if (primIt != rt.primitiveArrays.end()) {
            if (idx < primIt->second.size()) {
                primIt->second[idx] = parseIntValue(value).value_or(0);
            }
        } else {
            auto arrayIt = rt.arrays.find(*id);
            if (arrayIt != rt.arrays.end() && idx < arrayIt->second.size()) {
                arrayIt->second[idx] = value;
            }
        }
    }
    if (rt.trace.recording) rt.trace.arrayWrites.push_back(ArrayWrite{label, pc, arrayValue, indexValue, value});
}

NativeCallResult handleBuiltInInstanceCall(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (ref.className == "java/lang/Object" && ref.name == "getClass" &&
        ref.descriptor == "()Ljava/lang/Class;") {
        std::string className = ref.className;
        if (!args.empty()) {
            std::optional<uint32_t> id = objectId(args[0]);
            auto objectIt = id.has_value() ? rt.heap.find(*id) : rt.heap.end();
            if (id.has_value() && objectIt != rt.heap.end()) {
                className = objectIt->second.className;
            }
        }
        return NativeCallResult{true, Value::named(std::string(kClassHandlePrefix) + className)};
    }

    if (ref.className == "java/lang/Class" && ref.name == "getResourceAsStream" &&
        ref.descriptor == "(Ljava/lang/String;)Ljava/io/InputStream;") {
        std::string path = args.size() > 1 ? runtimeString(rt, args[1]).value_or(args[1].asText()) : "";
        if (path.empty()) {
            return NativeCallResult{true, Value::named("0")};
        }
        std::vector<uint8_t> data;
        if (!port::readResourceAll(path, data)) {
            return NativeCallResult{true, Value::named("0")};
        }
        return NativeCallResult{true, allocateByteArrayInputStream(rt, classes, "<resource>", 0, data)};
    }

    return NativeCallResult{};
}

std::optional<Value> recordUnknownCall(
    Runtime& rt,
    const std::string& label,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    std::optional<Value> result;
    if (returnsValue(ref.descriptor)) {
        result = Value::named("<call:" + callName(ref) + ">");
    }

    bool nooped = ref.name == "<init>" || !returnsValue(ref.descriptor);
    std::string caller = label;
    if (caller.empty() && !rt.callStack.empty()) {
        const RuntimeFrame& frame = rt.callStack.back();
        if (frame.cls != nullptr && frame.method != nullptr) {
            caller = methodLabel(*frame.cls, *frame.method);
        }
    }
    rt.trace.unknownMethodCalls.push_back(UnknownMethodCall{
        caller,
        pc,
        callName(ref),
        args,
        nooped,
        result.has_value() ? result->asText() : "",
    });
    return result;
}

std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix) {
    // Legacy string handle decoder — only used for class: / resource-stream: prefixes.
    if (value.tag != Value::Tag::kStr) return std::nullopt;
    const std::string& s = *value.str;
    if (s.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(s.c_str() + prefix.size(), &end, 10);
    if (end == nullptr || *end != '\0') return std::nullopt;
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> objectId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleObjTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

std::optional<uint32_t> arrayId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleArrTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

bool isReference(const Value& value) {
    if (value.tag != Value::Tag::kInt) return false;
    const int32_t t = value.i32 & Value::kHandleTagMask;
    return t == Value::kHandleObjTag || t == Value::kHandleArrTag;
}

std::string exceptionClassName(const Runtime& rt, const Value& exception) {
    std::optional<uint32_t> id = objectId(exception);
    if (!id.has_value()) {
        return exception.asText();
    }
    auto objectIt = rt.heap.find(*id);
    return objectIt == rt.heap.end() ? exception.asText() : objectIt->second.className;
}

void setPendingException(Runtime& rt, Value exception, const std::string& methodLabel, uint32_t pc) {
    rt.pendingException = std::move(exception);
    rt.pendingExceptionMethodLabel = methodLabel;
    rt.pendingExceptionPc = pc;
}

void clearPendingException(Runtime& rt) {
    rt.pendingException.reset();
    rt.pendingExceptionMethodLabel.clear();
    rt.pendingExceptionPc = 0;
}

const MethodInfo::ExceptionHandler* findExceptionHandler(
    const Runtime& rt,
    const std::vector<ClassFile>& classes,
    const MethodInfo& method,
    uint32_t throwPc) {
    if (!rt.pendingException.has_value()) {
        return nullptr;
    }
    const std::string thrownClass = exceptionClassName(rt, *rt.pendingException);
    for (const MethodInfo::ExceptionHandler& handler : method.exceptionHandlers) {
        if (throwPc < handler.startPc || throwPc >= handler.endPc) {
            continue;
        }
        if (handler.catchType == 0 ||
            isClassOrSubclassOf(classes, thrownClass, handler.catchClass)) {
            return &handler;
        }
    }
    return nullptr;
}

bool handlePendingExceptionAt(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const MethodInfo& method,
    Frame& frame,
    uint32_t throwPc,
    size_t& pc) {
    const MethodInfo::ExceptionHandler* handler =
        findExceptionHandler(rt, classes, method, throwPc);
    if (handler == nullptr) {
        return false;
    }
    if (rt.trace.recording) {
        rt.trace.caughtExceptions.push_back(CaughtExceptionTrace{
            rt.pendingExceptionMethodLabel,
            rt.pendingExceptionPc,
            methodLabel(*rt.callStack.back().cls, method),
            handler->handlerPc,
            exceptionClassName(rt, *rt.pendingException),
        });
    }
    Value exception = *rt.pendingException;
    clearPendingException(rt);
    frame.clearStack();
    frame.push(std::move(exception));
    pc = handler->handlerPc;
    return true;
}

void recordUncaughtException(Runtime& rt, const std::string& threadLabel) {
    if (!rt.trace.recording || !rt.pendingException.has_value()) {
        return;
    }
    rt.trace.uncaughtExceptions.push_back(UncaughtExceptionTrace{
        threadLabel,
        rt.pendingExceptionMethodLabel,
        rt.pendingExceptionPc,
        exceptionClassName(rt, *rt.pendingException),
    });
}

void recordThreadDeath(Runtime& rt, const std::string& threadLabel) {
    if (!rt.trace.recording || !rt.pendingException.has_value()) {
        return;
    }
    rt.trace.threadDeaths.push_back(ThreadDeathTrace{
        threadLabel,
        exceptionClassName(rt, *rt.pendingException),
    });
}

std::string debugValueText(const Runtime& rt, const Value& value) {
    std::optional<uint32_t> id = objectId(value);
    if (id.has_value()) {
        auto stringIt = rt.strings.find(*id);
        if (stringIt != rt.strings.end()) {
            return '"' + stringIt->second + '"';
        }
    }
    return value.asText();
}

void recordTaskMethodProfile(
    Runtime& rt,
    const ClassFile& cls,
    const MethodInfo& method,
    uint32_t elapsedUs) {
    for (MethodProfileAccumulator& entry : rt.taskMethodProfiles) {
        if (entry.cls == &cls && entry.method == &method) {
            ++entry.calls;
            entry.totalUs += elapsedUs;
            if (elapsedUs > entry.maxUs) entry.maxUs = elapsedUs;
            return;
        }
    }
    rt.taskMethodProfiles.push_back(MethodProfileAccumulator{&cls, &method, 1, elapsedUs, elapsedUs});
}

void captureTaskMethodProfiles(Runtime& rt) {
    if (rt.taskMethodProfiles.empty()) return;

    std::vector<const MethodProfileAccumulator*> sorted;
    sorted.reserve(rt.taskMethodProfiles.size());
    for (const MethodProfileAccumulator& entry : rt.taskMethodProfiles) {
        sorted.push_back(&entry);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const MethodProfileAccumulator* a, const MethodProfileAccumulator* b) {
                  return a->totalUs > b->totalUs;
              });

    const uint8_t limit = rt.host != nullptr ? rt.host->profileTaskMethodLimit : 10;
    const size_t count = sorted.size() < limit ? sorted.size() : limit;
    rt.trace.taskMethodProfiles.clear();
    rt.trace.taskMethodProfiles.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const MethodProfileAccumulator& entry = *sorted[i];
        rt.trace.taskMethodProfiles.push_back(MethodProfile{
            methodLabel(*entry.cls, *entry.method),
            entry.calls,
            entry.totalUs,
            entry.maxUs,
        });
    }
}

void recordTaskNativeProfile(Runtime& rt, const MethodRef& ref, uint32_t elapsedUs) {
    const std::string label = callName(ref);
    for (NamedProfileAccumulator& entry : rt.taskNativeProfiles) {
        if (entry.label == label) {
            ++entry.calls;
            entry.totalUs += elapsedUs;
            if (elapsedUs > entry.maxUs) entry.maxUs = elapsedUs;
            return;
        }
    }
    rt.taskNativeProfiles.push_back(NamedProfileAccumulator{label, 1, elapsedUs, elapsedUs});
}

void captureTaskNativeProfiles(Runtime& rt) {
    if (rt.taskNativeProfiles.empty()) return;

    std::vector<const NamedProfileAccumulator*> sorted;
    sorted.reserve(rt.taskNativeProfiles.size());
    for (const NamedProfileAccumulator& entry : rt.taskNativeProfiles) {
        sorted.push_back(&entry);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const NamedProfileAccumulator* a, const NamedProfileAccumulator* b) {
                  return a->totalUs > b->totalUs;
              });

    const uint8_t limit = rt.host != nullptr ? rt.host->profileTaskMethodLimit : 10;
    const size_t count = sorted.size() < limit ? sorted.size() : limit;
    rt.trace.taskNativeProfiles.clear();
    rt.trace.taskNativeProfiles.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const NamedProfileAccumulator& entry = *sorted[i];
        rt.trace.taskNativeProfiles.push_back(MethodProfile{
            entry.label,
            entry.calls,
            entry.totalUs,
            entry.maxUs,
        });
    }
}

void requestThreadYield(Runtime& rt, uint32_t millis) {
    rt.yieldRequested = true;
    rt.yieldMillis = millis;
}

void restoreCallStackReserve(Runtime& rt) {
    if (rt.callStack.capacity() < kMaxCallDepth + 1) {
        rt.callStack.reserve(kMaxCallDepth + 1);
    }
}

// Yield: copy each callStack frame's [locals... stack...] slots out of the
// arena into RuntimeFrame::suspendedSlots, then move the frames into the
// task's suspendedFrames container. Arena is implicitly reusable as soon as
// callStack becomes empty (allocations restart at arena.begin()).
void suspendCallStackInto(Runtime& rt, ThreadTask& task) {
    for (RuntimeFrame& rf : rt.callStack) {
        const size_t total = rf.frame.localsSize() + rf.frame.stackSize();
        rf.suspendedSlots.clear();
        rf.suspendedSlots.reserve(total);
        Value* begin = rf.frame.slotsBegin();
        for (size_t i = 0; i < total; ++i) {
            rf.suspendedSlots.emplace_back(std::move(begin[i]));
        }
    }
    task.suspendedFrames = std::move(rt.callStack);
    rt.callStack.clear();
    restoreCallStackReserve(rt);
}

// Resume: rebuild rt.callStack from task.suspendedFrames, copying each
// frame's suspendedSlots back into freshly-bumped arena regions and
// rebinding the Frame view. Frames are restored in original order (caller
// first) so each allocation derives its base from the just-restored top.
// The total slot footprint matches what was previously live in the arena,
// so a fresh arena (callStack empty between tasks) is guaranteed to fit.
void resumeCallStackFrom(Runtime& rt, ThreadTask& task) {
    std::vector<RuntimeFrame, SramAllocator<RuntimeFrame>> source = std::move(task.suspendedFrames);
    task.suspendedFrames.clear();
    rt.callStack.clear();
    restoreCallStackReserve(rt);
    Value* arenaEnd = rt.frameArena.end();
    for (RuntimeFrame& rf : source) {
        Value* base = rt.callStack.empty()
            ? rt.frameArena.begin()
            : rt.callStack.back().frame.stackEnd();
        const uint16_t maxLocals = static_cast<uint16_t>(rf.frame.localsSize());
        const uint16_t maxStack = rf.frame.maxStack();
        const size_t total = rf.suspendedSlots.size();
        const size_t stackUsed = total >= maxLocals ? total - maxLocals : 0;
        for (size_t i = 0; i < total; ++i) {
            base[i] = std::move(rf.suspendedSlots[i]);
        }
        rf.suspendedSlots.clear();
        rf.suspendedSlots.shrink_to_fit();
        rf.frame = Frame(base, arenaEnd, maxLocals, maxStack, stackUsed);
        rt.callStack.push_back(std::move(rf));
    }
}

Value internString(Runtime& rt, const std::string& text) {
    auto internIt = rt.internedStrings.find(text);
    if (internIt != rt.internedStrings.end() && rt.strings.find(internIt->second) != rt.strings.end()) {
        return objectRef(internIt->second);
    }

    uint32_t id = allocateHeapObjectId(rt, "java/lang/String");
    rt.strings[id] = text;
    rt.internedStrings[text] = id;
    return objectRef(id);
}

void markValue(
    const Value& value,
    const Runtime& rt,
    std::set<uint32_t>& markedObjects,
    std::set<uint32_t>& markedArrays) {
    std::optional<uint32_t> obj = objectId(value);
    if (obj.has_value()) {
        if (!markedObjects.insert(*obj).second) {
            return;
        }
        auto objectIt = rt.heap.find(*obj);
        if (objectIt == rt.heap.end()) {
            return;
        }
        for (const Value& fieldVal : objectIt->second.fields) {
            markValue(fieldVal, rt, markedObjects, markedArrays);
        }
        return;
    }

    std::optional<uint32_t> arr = arrayId(value);
    if (arr.has_value()) {
        if (!markedArrays.insert(*arr).second) {
            return;
        }
        auto arrayIt = rt.arrays.find(*arr);
        if (arrayIt == rt.arrays.end()) {
            return;
        }
        for (const Value& element : arrayIt->second) {
            markValue(element, rt, markedObjects, markedArrays);
        }
    }
}

void addRoot(
    GcReport& report,
    const std::string& name,
    const Value& value,
    const Runtime& rt,
    std::set<uint32_t>& markedObjects,
    std::set<uint32_t>& markedArrays) {
    if (!isReference(value)) {
        return;
    }
    report.roots.push_back(name + "=" + value.asText());
    markValue(value, rt, markedObjects, markedArrays);
}

void collectGarbage(Runtime& rt, std::string when) {
    const auto startedAt = std::chrono::steady_clock::now();
    GcReport report;
    report.when = std::move(when);

    std::set<uint32_t> markedObjects;
    std::set<uint32_t> markedArrays;
    for (const auto& field : rt.staticFields) {
        addRoot(report, "static " + field.first, field.second, rt, markedObjects, markedArrays);
    }
    if (rt.pendingException.has_value()) {
        addRoot(report, "pendingException", *rt.pendingException, rt, markedObjects, markedArrays);
    }
    for (const RuntimeFrame& runtimeFrame : rt.callStack) {
        const Value* locals = runtimeFrame.frame.localsData();
        const size_t localsSize = runtimeFrame.frame.localsSize();
        for (size_t i = 0; i < localsSize; ++i) {
            addRoot(report, runtimeFrame.label + " local[" + std::to_string(i) + "]", locals[i], rt, markedObjects, markedArrays);
        }

        const Value* stack = runtimeFrame.frame.stackData();
        const size_t stackSize = runtimeFrame.frame.stackSize();
        for (size_t i = 0; i < stackSize; ++i) {
            addRoot(report, runtimeFrame.label + " stack[" + std::to_string(i) + "]", stack[i], rt, markedObjects, markedArrays);
        }
    }
    if (rt.session != nullptr) {
        for (const ThreadTask& task : rt.session->tasks()) {
            if (task.pendingException.has_value()) {
                addRoot(report, "task pendingException", *task.pendingException, rt, markedObjects, markedArrays);
            }
            for (const RuntimeFrame& runtimeFrame : task.suspendedFrames) {
                // Suspended frames' arena slots have been copied out; walk
                // the side buffer instead. Layout: [locals..., stack...].
                const std::vector<Value>& slots = runtimeFrame.suspendedSlots;
                const size_t localsSize = runtimeFrame.frame.localsSize();
                const size_t total = slots.size();
                const size_t suspendedLocals = std::min(localsSize, total);
                for (size_t i = 0; i < suspendedLocals; ++i) {
                    addRoot(report, runtimeFrame.label + " local[" + std::to_string(i) + "]", slots[i], rt, markedObjects, markedArrays);
                }
                for (size_t i = suspendedLocals; i < total; ++i) {
                    addRoot(report, runtimeFrame.label + " stack[" + std::to_string(i - suspendedLocals) + "]", slots[i], rt, markedObjects, markedArrays);
                }
            }
        }
    }

    std::vector<uint32_t> objectsToFree;
    for (const auto& object : rt.heap) {
        if (markedObjects.find(object.first) == markedObjects.end()) {
            if (object.second.className == "java/lang/String") {
                report.unreachableStrings.push_back(objectRef(object.first));
            } else {
                report.unreachableObjects.push_back(objectRef(object.first));
            }
            objectsToFree.push_back(object.first);
        }
    }
    std::vector<uint32_t> arraysToFree;
    for (const auto& array : rt.arrays) {
        if (markedArrays.find(array.first) == markedArrays.end()) {
            report.unreachableArrays.push_back(arrayRef(array.first));
            arraysToFree.push_back(array.first);
        }
    }
    for (const auto& array : rt.primitiveArrays) {
        if (markedArrays.find(array.first) == markedArrays.end()) {
            report.unreachableArrays.push_back(arrayRef(array.first));
            arraysToFree.push_back(array.first);
        }
    }

    for (uint32_t id : objectsToFree) {
        auto objectIt = rt.heap.find(id);
        const bool isString = objectIt != rt.heap.end() && objectIt->second.className == "java/lang/String";
        auto strIt = rt.strings.find(id);
        if (strIt != rt.strings.end()) {
            rt.internedStrings.erase(strIt->second);
            rt.strings.erase(strIt);
        }
        rt.heap.erase(id);
        rt.freeObjectIds.push_back(id);
        if (isString) {
            report.freedStrings.push_back(objectRef(id));
        } else {
            report.freedObjects.push_back(objectRef(id));
        }
    }
    for (uint32_t id : arraysToFree) {
        if (rt.primitiveArrays.erase(id) == 0) {
            rt.arrays.erase(id);
        }
        rt.freeArrayIds.push_back(id);
        report.freedArrays.push_back(arrayRef(id));
    }

    report.durationMillis = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count());
    rt.trace.gcReports.push_back(report);
}

Value readFieldValue(Runtime& rt, const Value& object, const std::string& fieldName) {
    std::optional<uint32_t> id = objectId(object);
    if (!id.has_value()) return Value::ofInt(0);
    auto objectIt = rt.heap.find(*id);
    if (objectIt == rt.heap.end()) return Value::ofInt(0);
    const HeapObject& obj = objectIt->second;
    auto slotCacheIt = rt.fieldSlotCache.find(obj.cls);
    if (slotCacheIt == rt.fieldSlotCache.end()) return Value::ofInt(0);
    auto nameIt = slotCacheIt->second.find(fieldName);
    if (nameIt == slotCacheIt->second.end()) return Value::ofInt(0);
    uint16_t slot = nameIt->second;
    if (slot >= obj.fields.size() || !obj.fields[slot].isInitialized()) return Value::ofInt(0);
    return obj.fields[slot];
}

// Forward declarations for the trampoline-based dispatch. resumeCurrentMethod
// no longer recurses; it pushes new Java frames onto rt.callStack and returns
// to the trampoline loop, which re-enters it on the newly-pushed frame. This
// keeps the C++ stack flat regardless of Java call depth.
std::optional<Value> resumeCurrentMethod(
    const std::vector<ClassFile>& classes,
    Runtime& rt);
bool pushJavaFrame(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    std::vector<Value>& args);
void runTrampoline(const std::vector<ClassFile>& classes, Runtime& rt);
void runTrampolineUntilSize(
    const std::vector<ClassFile>& classes,
    Runtime& rt,
    size_t targetSize);

void ensureClassInitialized(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    Runtime& rt) {
    if (!rt.initializedClasses.insert(className).second) return;
    const ClassFile* cls = findClass(classes, className);
    if (cls == nullptr) return;
    if (!cls->superClass.empty())
        ensureClassInitialized(classes, cls->superClass, rt);
    const MethodInfo* clinit = findDeclaredMethod(*cls, "<clinit>", "()V");
    if (clinit == nullptr) return;
    std::vector<Value> noArgs;
    const size_t base = rt.callStack.size();
    if (!pushJavaFrame(rt, classes, *cls, *clinit, noArgs)) return;
    runTrampolineUntilSize(classes, rt, base);
}

void queueRunnableTask(Runtime& rt, const std::vector<ClassFile>& classes, const Value& runnable) {
    std::optional<uint32_t> id = objectId(runnable);
    if (!id.has_value()) {
        return;
    }
    auto objectIt = rt.heap.find(*id);
    if (objectIt == rt.heap.end()) {
        return;
    }
    const ClassFile* owner = nullptr;
    const MethodInfo* run = findMethodInHierarchy(classes, objectIt->second.className, "run", "()V", &owner);
    if (owner == nullptr || run == nullptr) {
        return;
    }
    if (rt.session != nullptr) {
        LOGF_W("[task-queued] %s.%s%s (pre-size=%zu)", owner->thisClass.c_str(), run->name.c_str(), run->descriptor.c_str(), rt.session->tasks().size());
        ThreadTask task;
        task.cls = owner;
        task.method = run;
        task.receiver = runnable;
        rt.session->tasks().push_back(std::move(task));
        return;
    }
    std::vector<Value> runArgs = {runnable};
    const size_t base = rt.callStack.size();
    if (!pushJavaFrame(rt, classes, *owner, *run, runArgs)) return;
    runTrampolineUntilSize(classes, rt, base);
}

void initializeFrameArgs(RuntimeFrame& runtimeFrame, std::vector<Value>& args) {
    Frame& frame = runtimeFrame.frame;
    const MethodInfo& method = *runtimeFrame.method;
    if (args.empty()) {
        for (size_t i = 0; i < argumentSlots(method) && i < method.maxLocals; ++i) {
            frame.setLocal(static_cast<uint16_t>(i),
                           Value::named("<arg:" + localNameAt(method, static_cast<uint16_t>(i), 0) + ">"));
        }
        return;
    }

    size_t localIndex = hasAccess(method.access, 0x0008) ? 0 : 1;
    size_t argIndex = 0;
    if (!hasAccess(method.access, 0x0008) && !args.empty() && method.maxLocals > 0) {
        frame.setLocal(0, std::move(args[0]));
        argIndex = 1;
    }
    std::vector<size_t> widths = argumentSlotWidths(method.descriptor);
    for (size_t i = 0; i < widths.size() && argIndex < args.size() && localIndex < method.maxLocals; ++i) {
        frame.setLocal(static_cast<uint16_t>(localIndex), std::move(args[argIndex]));
        localIndex += widths[i];
        ++argIndex;
    }
}

bool isAssignableTo(const std::vector<ClassFile>& classes,
                    const std::string& objClass,
                    const std::string& targetClass,
                    int depth = 0) {
    if (objClass == targetClass) return true;
    if (depth > 16) return false;
    const ClassFile* cls = findClass(classes, objClass);
    if (cls == nullptr) return false;
    if (!cls->superClass.empty() && isAssignableTo(classes, cls->superClass, targetClass, depth + 1))
        return true;
    for (const std::string& iface : cls->interfaces)
        if (isAssignableTo(classes, iface, targetClass, depth + 1)) return true;
    return false;
}

// Sets up a new RuntimeFrame for the given Java method, pushes it onto
// rt.callStack, and returns true. On Java call depth limit or frame-arena
// overflow, raises a StackOverflowError via setPendingException and returns
// false — the caller is responsible for invoking its own exception handler
// search (catchPendingException) before continuing.
bool pushJavaFrame(
    Runtime& rt,
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    std::vector<Value>& args) {
    if (rt.callStack.size() >= kMaxCallDepth) {
        Value soe = allocateObject(rt, classes, "<push>", 0, "java/lang/StackOverflowError");
        setPendingException(rt, std::move(soe), "", 0);
        return false;
    }
    Value* slabBase = rt.callStack.empty()
        ? rt.frameArena.begin()
        : rt.callStack.back().frame.stackEnd();
    Value* arenaEnd = rt.frameArena.end();
    const size_t needed = static_cast<size_t>(method.maxLocals) + static_cast<size_t>(method.maxStack);
    if (slabBase + needed > arenaEnd) {
        Value soe = allocateObject(rt, classes, "<push>", 0, "java/lang/StackOverflowError");
        setPendingException(rt, std::move(soe), "", 0);
        return false;
    }
    std::string label = rt.trace.recording ? methodLabel(cls, method) : std::string{};
    RuntimeFrame frame{
        std::move(label), &cls, &method, 0, SIZE_MAX,
        Frame(slabBase, arenaEnd, method.maxLocals, method.maxStack),
        {}};
    initializeFrameArgs(frame, args);
    rt.callStack.push_back(std::move(frame));
    return true;
}

std::optional<Value> resumeCurrentMethod(
    const std::vector<ClassFile>& classes,
    Runtime& rt) {
    if (rt.callStack.empty()) {
        return std::nullopt;
    }

    RuntimeFrame& runtimeFrame = rt.callStack.back();
    const ClassFile& cls = *runtimeFrame.cls;
    const MethodInfo& method = *runtimeFrame.method;
    const std::string& label = runtimeFrame.label;
    Frame& frame = runtimeFrame.frame;
    size_t pc = runtimeFrame.pc;

    struct ScopedTaskMethodProfile {
        Runtime& rt;
        const ClassFile& cls;
        const MethodInfo& method;
        uint32_t startedUs;
        bool active;

        ~ScopedTaskMethodProfile() {
            if (!active) return;
            const uint32_t endedUs = nowUs();
            if (rt.pendingYieldRethrowStartUs != 0) {
                rt.trace.frameProfile.taskYieldUnwindUs += endedUs - rt.pendingYieldRethrowStartUs;
                rt.pendingYieldRethrowStartUs = 0;
            }
            recordTaskMethodProfile(rt, cls, method, endedUs - startedUs);
        }
    } taskMethodProfile{
        rt,
        cls,
        method,
        (rt.host != nullptr && rt.host->profileTaskMethods && rt.currentTask != nullptr) ? nowUs() : 0u,
        rt.host != nullptr && rt.host->profileTaskMethods && rt.currentTask != nullptr,
    };

    auto finish = [&](std::optional<Value> result) -> std::optional<Value> {
        rt.callStack.pop_back();
        return result;
    };

    auto recordLocal = [&](uint16_t index, uint32_t writePc, const Value& value, const std::string& reason) {
        if (!rt.trace.recording) return;
        rt.trace.localWrites.push_back(LocalWrite{label, writePc, index, localNameAt(method, index, writePc), value, reason});
    };

    auto store = [&](uint16_t index, uint32_t writePc) {
        Value value = frame.pop();
        frame.setLocal(index, value);
        recordLocal(index, writePc, value, "store");
    };

    auto catchPendingException = [&](uint32_t throwPc) {
        if (!handlePendingExceptionAt(rt, classes, method, frame, throwPc, pc)) {
            return false;
        }
        runtimeFrame.pc = pc;
        return true;
    };

    auto invokeLength = [&](uint8_t invokeOp) -> size_t {
        return invokeOp == 0xb9 ? 5u : 3u;
    };

    auto invokeDisplayableNotify = [&](const Value& displayable, const char* methodName) {
        std::optional<uint32_t> id = objectId(displayable);
        if (!id.has_value()) {
            return;
        }
        auto objectIt = rt.heap.find(*id);
        if (objectIt == rt.heap.end()) {
            return;
        }
        const ClassFile* owner = nullptr;
        const MethodInfo* notify = findMethodInHierarchy(
            classes,
            objectIt->second.className,
            methodName,
            "()V",
            &owner);
        if (owner == nullptr || notify == nullptr) {
            return;
        }
        std::vector<Value> notifyArgs = {displayable};
        const size_t notifyBase = rt.callStack.size();
        if (!pushJavaFrame(rt, classes, *owner, *notify, notifyArgs)) return;
        runTrampolineUntilSize(classes, rt, notifyBase);
    };

    auto makeNativeContext = [&]() {
        return NativeCallContext{
            rt.host,
            &classes,
            rt.trace,
            [&](const Value& object, const std::string& fieldName) {
                return readFieldValue(rt, object, fieldName);
            },
            [&](const Value& runnable) {
                queueRunnableTask(rt, classes, runnable);
            },
            [&](uint32_t millis) {
                if (rt.currentTask != nullptr) {
                    requestThreadYield(rt, millis);
                    return;
                }
                if (rt.host != nullptr) {
                    rt.host->sleepMillis(millis);
                }
            },
            [&]() {
                rt.repaintRequested = true;
            },
            rt.gameCanvasFlushCommitted,
            [&](const Value& oldDisplayable, const Value& newDisplayable) {
                if (oldDisplayable.asText() == newDisplayable.asText()) {
                    return;
                }
                invokeDisplayableNotify(oldDisplayable, "hideNotify");
                if (rt.pendingException.has_value()) {
                    return;
                }
                invokeDisplayableNotify(newDisplayable, "showNotify");
                rt.repaintRequested = true;
            },
            rt.strings,
            rt.arrays,
            rt.primitiveArrays,
            rt.images,
            rt.resourceImages,
            rt.nextImageId,
            rt.displayRef,
            rt.currentDisplayable,
            rt.graphicsFramebuffer,
            rt.graphicsWidth,
            rt.graphicsHeight,
            rt.graphicsColorRgb,
            {},
            [&](std::string when) {
                collectGarbage(rt, std::move(when));
            },
            [&](const std::string& text) {
                return internString(rt, text);
            },
        };
    };

    // Cache bytecode as a raw pointer so that all inner reads resolve to a single
    // pointer dereference rather than going through the std::vector metadata.
    // On ESP32, if the method code exceeds the PSRAM threshold (4 KB), copy it
    // into internal SRAM so the CPU never has to fetch bytecodes from PSRAM.
    const size_t codeSize = method.code.size();
    const uint8_t* code = method.code.data();
#ifdef ESP32_BUILD
    uint8_t* sramCode = nullptr;
    if (codeSize > 4096) {
        sramCode = static_cast<uint8_t*>(
            heap_caps_malloc(codeSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
        if (sramCode != nullptr) {
            memcpy(sramCode, code, codeSize);
            code = sramCode;
        }
    }
    struct SramCodeGuard {
        uint8_t* ptr;
        ~SramCodeGuard() { if (ptr != nullptr) heap_caps_free(ptr); }
    } sramCodeGuard{sramCode};
#endif

    // Build the NativeCallContext once; it holds references/lambdas that stay
    // valid for the lifetime of this executeMethod call. We reuse it for every
    // native dispatch rather than constructing it (and its std::function members)
    // anew for each bytecode instruction.
    NativeCallContext sharedNativeCtx = makeNativeContext();
    // Point to the Runtime-owned main-framebuffer Canvas so translate/clip state
    // persists across nested executeMethod() calls within the same paint() frame.
    sharedNativeCtx.mainFbCanvas = rt.mainFbCanvas.has_value() ? &rt.mainFbCanvas.value() : nullptr;

    // When no host is attached (or host has no stats to collect) skip all
    // per-bytecode nowUs() calls: each costs ~6µs and there are ~13 000/frame.
    const bool kCollectStats =
#if JVM_ENABLE_BYTECODE_PROFILING
        (rt.host != nullptr && rt.host->collectBytecodeStats);
#else
        false;
#endif
    // Zero-cost wrapper: returns nowUs() only when stats are active, 0 otherwise.
    // "if (kCollectStats)" is a compile-time-predictable branch; the nowUs() call
    // is completely absent from the hot path when kCollectStats==false.
    auto statNow = [&]() -> uint32_t {
        return kCollectStats ? nowUs() : 0u;
    };

    // Trampoline re-entry exception guard: when the previous resumeCurrentMethod
    // call popped a callee that threw and didn't catch, the trampoline re-enters
    // this frame with rt.pendingException still set. Try this frame's exception
    // table at the invoke pc (lastCallPc); if no handler matches, propagate up.
    if (rt.pendingException.has_value() && runtimeFrame.lastCallPc != SIZE_MAX) {
        const uint32_t throwPc = static_cast<uint32_t>(runtimeFrame.lastCallPc);
        runtimeFrame.lastCallPc = SIZE_MAX;
        if (!catchPendingException(throwPc)) {
            runtimeFrame.pc = throwPc;
            return finish(std::nullopt);
        }
        // catchPendingException succeeded: pc is now at handler, runtimeFrame.pc
        // was updated, rt.pendingException cleared. Fall through to bytecode loop.
    }

    while (pc < codeSize) {
        // Batched step-limit check: increment every step, but only consult
        // kMaxSteps every 256 steps. kMaxSteps is a soft yield boundary
        // (currently 100000), so ±256 drift is irrelevant. The hot path
        // pays one increment + a single low-byte test; the (rt.steps & 0xFF)
        // == 0 branch is taken 255 of every 256 steps, so the branch
        // predictor learns it cleanly and the slow body stays cold.
        if ((++rt.steps & 0xFFu) == 0 && rt.steps > kMaxSteps) {
            rt.trace.stepLimitHit = true;
            if (!rt.stepLimitYieldEnabled) {
                rt.steps = 0;
                continue;
            }
            runtimeFrame.pc = pc;
            captureStackSnapshot(rt.trace, rt.callStack);
            requestThreadYield(rt, 1);
            return std::nullopt;
        }
#if JVM_ENABLE_BYTECODE_PROFILING
        if (rt.host != nullptr && rt.host->collectBytecodeStats) ++rt.host->bytecodeSteps;
#endif

        uint8_t op = code[pc];
        switch (op) {
            case 0x01: { const uint32_t t0=statNow(); frame.push(Value::ofInt(0)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // aconst_null
            case 0x02: { const uint32_t t0=statNow(); frame.push(Value::ofInt(-1)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // iconst_m1
            case 0x03: { const uint32_t t0=statNow(); frame.push(Value::ofInt(0)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_0
            case 0x04: { const uint32_t t0=statNow(); frame.push(Value::ofInt(1)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_1
            case 0x05: { const uint32_t t0=statNow(); frame.push(Value::ofInt(2)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_2
            case 0x06: { const uint32_t t0=statNow(); frame.push(Value::ofInt(3)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_3
            case 0x07: { const uint32_t t0=statNow(); frame.push(Value::ofInt(4)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_4
            case 0x08: { const uint32_t t0=statNow(); frame.push(Value::ofInt(5)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }   // iconst_5
            case 0x09: { const uint32_t t0=statNow(); frame.push(Value::ofLong(0)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // lconst_0
            case 0x0a: { const uint32_t t0=statNow(); frame.push(Value::ofLong(1)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // lconst_1
            case 0x0b: { const uint32_t t0=statNow(); frame.push(Value::ofInt(0));          ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // fconst_0
            case 0x0c: { const uint32_t t0=statNow(); frame.push(Value::ofInt(0x3F800000)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // fconst_1
            case 0x0d: { const uint32_t t0=statNow(); frame.push(Value::ofInt(0x40000000)); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // fconst_2
            case 0x0e: { const uint32_t t0=statNow(); frame.push(Value::ofLong(INT64_C(0)));                    ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // dconst_0
            case 0x0f: { const uint32_t t0=statNow(); frame.push(Value::ofLong(INT64_C(0x3FF0000000000000))); ++pc; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // dconst_1
            case 0x10: { const uint32_t t0=statNow(); frame.push(Value::ofInt(codeS1(code, pc + 1))); pc += 2; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // bipush
            case 0x11: { const uint32_t t0=statNow(); frame.push(Value::ofInt(codeS2(code, pc + 1))); pc += 3; if(t0) rt.host->pushStats.record(nowUs()-t0); break; }  // sipush
            case 0x12: {
                const uint32_t t0=statNow();
                uint16_t index = codeU1(code, pc + 1);
                frame.push(cls.cp[index].tag == CpInteger
                    ? Value::ofInt(resolveIntegerConstant(cls, index))
                    : internString(rt, resolveStringConstant(cls, index)));
                pc += 2;
                if(t0) rt.host->pushStats.record(nowUs()-t0);
                break;
            }
            case 0x13: {
                const uint32_t t0=statNow();
                uint16_t index = codeU2(code, pc + 1);
                frame.push(cls.cp[index].tag == CpInteger
                    ? Value::ofInt(resolveIntegerConstant(cls, index))
                    : internString(rt, resolveStringConstant(cls, index)));
                pc += 3;
                if(t0) rt.host->pushStats.record(nowUs()-t0);
                break;
            }
            case 0x14: {
                const uint32_t t0=statNow();
                uint16_t index = codeU2(code, pc + 1);
                frame.push(Value::ofLong(resolveLongConstant(cls, index)));
                pc += 3;
                if(t0) rt.host->pushStats.record(nowUs()-t0);
                break;
            }

            case 0x1e: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x1f: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x20: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x21: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x1a: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x1b: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x1c: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x1d: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x16: { const uint32_t t0 = statNow(); frame.push(frame.local(codeU1(code, pc + 1))); pc += 2; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x15: { const uint32_t t0 = statNow(); frame.push(frame.local(codeU1(code, pc + 1))); pc += 2; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x19: { const uint32_t t0 = statNow(); frame.push(frame.local(codeU1(code, pc + 1))); pc += 2; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x22: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // fload_0
            case 0x23: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // fload_1
            case 0x24: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // fload_2
            case 0x25: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // fload_3
            case 0x26: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // dload_0
            case 0x27: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // dload_1
            case 0x28: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // dload_2
            case 0x29: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // dload_3
            case 0x17: { const uint32_t t0 = statNow(); frame.push(frame.local(codeU1(code, pc + 1))); pc += 2; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // fload
            case 0x18: { const uint32_t t0 = statNow(); frame.push(frame.local(codeU1(code, pc + 1))); pc += 2; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }  // dload
            case 0x2a: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2b: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2c: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2d: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }

            case 0x2e:
            case 0x2f:
            case 0x30:  // faload
            case 0x31:  // daload
            case 0x32:
            case 0x33:
            case 0x34:
            case 0x35: {  // saload
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                const uint32_t t0 = statNow();
                frame.push(loadArrayElement(rt, arrayValue, indexValue));
                if(t0) rt.host->arrayLoadStats.record(nowUs() - t0);
                ++pc;
                break;
            }

            case 0x3f: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x40: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x41: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x42: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x3b: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x3c: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x3d: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x3e: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x37: { const uint32_t t0=statNow(); store(codeU1(code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x36: { const uint32_t t0=statNow(); store(codeU1(code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x3a: { const uint32_t t0=statNow(); store(codeU1(code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x38: { const uint32_t t0=statNow(); store(codeU1(code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // fstore
            case 0x39: { const uint32_t t0=statNow(); store(codeU1(code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // dstore
            case 0x43: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // fstore_0
            case 0x44: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // fstore_1
            case 0x45: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // fstore_2
            case 0x46: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // fstore_3
            case 0x47: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // dstore_0
            case 0x48: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // dstore_1
            case 0x49: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // dstore_2
            case 0x4a: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }  // dstore_3
            case 0x4b: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4c: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4d: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4e: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }

            case 0x4f:
            case 0x50:
            case 0x51:  // fastore
            case 0x52:  // dastore
            case 0x53:
            case 0x54:
            case 0x55:
            case 0x56: {  // sastore
                const uint32_t t0=statNow();
                uint32_t writePc = static_cast<uint32_t>(pc);
                Value value = frame.pop();
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                storeArrayElement(rt, label, writePc, arrayValue, indexValue, value, op == 0x54);
                ++pc;
                if(t0) rt.host->arrayStoreStats.record(nowUs()-t0);
                break;
            }

            case 0x59: {
                Value value = frame.pop();
                frame.push(value);
                frame.push(value);
                ++pc;
                break;
            }

            case 0x5a: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                frame.push(value1);
                frame.push(value2);
                frame.push(value1);
                ++pc;
                break;
            }

            case 0x5b: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                Value value3 = frame.pop();
                frame.push(value1);
                frame.push(value3);
                frame.push(value2);
                frame.push(value1);
                ++pc;
                break;
            }

            case 0x5c: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                frame.push(value2);
                frame.push(value1);
                frame.push(value2);
                frame.push(value1);
                ++pc;
                break;
            }

            case 0x5d: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                Value value3 = frame.pop();
                frame.push(value2);
                frame.push(value1);
                frame.push(value3);
                frame.push(value2);
                frame.push(value1);
                ++pc;
                break;
            }

            case 0x5e: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                Value value3 = frame.pop();
                Value value4 = frame.pop();
                frame.push(value2);
                frame.push(value1);
                frame.push(value4);
                frame.push(value3);
                frame.push(value2);
                frame.push(value1);
                ++pc;
                break;
            }

            case 0x5f: {
                Value value1 = frame.pop();
                Value value2 = frame.pop();
                frame.push(value1);
                frame.push(value2);
                ++pc;
                break;
            }

            case 0x57: {
                const uint32_t t0m=statNow(); (void)frame.pop(); ++pc; if(t0m) rt.host->miscStats.record(nowUs()-t0m); break;
            }
            case 0x58: {
                Value top = frame.pop();
                if (top.tag != Value::Tag::kLong) (void)frame.pop();
                ++pc; break;  // pop2
            }

            case 0x84: {
                const uint32_t t0arith = statNow();
                uint16_t index = codeU1(code, pc + 1);
                int delta = codeS1(code, pc + 2);
                Value oldValue = frame.local(index);
                std::optional<int> oldInt = parseIntValue(oldValue);
                Value newValue = oldInt
                    ? Value::ofInt(*oldInt + delta)
                    : Value::named("(" + oldValue.asText() + " + " + std::to_string(delta) + ")");
                frame.setLocal(index, newValue);
                recordLocal(index, static_cast<uint32_t>(pc), newValue, "iinc");
                pc += 3;
                if(t0arith) rt.host->arithStats.record(nowUs() - t0arith);
                break;
            }

            case 0x60:
            case 0x61:
            case 0x64:
            case 0x65:
            case 0x68:
            case 0x69:
            case 0x6c:
            case 0x6d:
            case 0x70:
            case 0x71: {
                const uint32_t t0arith2 = statNow();
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                const char* opText = (op == 0x60 || op == 0x61) ? "+" :
                    (op == 0x64 || op == 0x65) ? "-" :
                    (op == 0x68 || op == 0x69) ? "*" :
                    (op == 0x6c || op == 0x6d) ? "/" : "%";
                std::optional<int> left = parseIntValue(lhs);
                std::optional<int> right = parseIntValue(rhs);
                if (op == 0x70 && left.has_value() && right.has_value()) {
                    frame.push(*right == 0 ? Value::named("<divide-by-zero>") : Value::ofInt(*left % *right));
                } else if (op == 0x71) {
                    std::optional<long long> leftLong = parseLongValue(lhs);
                    std::optional<long long> rightLong = parseLongValue(rhs);
                    frame.push(leftLong.has_value() && rightLong.has_value() && *rightLong != 0
                        ? Value::ofLong(*leftLong % *rightLong)
                        : leftLong.has_value() && rightLong.has_value() && *rightLong == 0
                            ? Value::named("<divide-by-zero>")
                            : Value::named("(" + lhs.asText() + " % " + rhs.asText() + ")"));
                } else if (op == 0x61 || op == 0x65 || op == 0x69 || op == 0x6d) {
                    frame.push(longBinaryOp(lhs, rhs, opText, op));
                } else {
                    frame.push(intBinaryOp(lhs, rhs, opText, op));
                }
                if(t0arith2) rt.host->arithStats.record(nowUs() - t0arith2);
                ++pc;
                break;
            }

            case 0x74: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value()
                    ? Value::ofInt(-*parsed)
                    : Value::named("(-" + value.asText() + ")"));
                ++pc;
                break;
            }

            case 0x75: {
                Value value = frame.pop();
                std::optional<long long> parsed = parseLongValue(value);
                frame.push(parsed.has_value()
                    ? Value::ofLong(-*parsed)
                    : Value::named("(-" + value.asText() + ")"));
                ++pc;
                break;
            }

            case 0x76: {  // fneg
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofInt(bitCast<int32_t>(-bitCast<float>(v.value_or(0)))));
                ++pc; break;
            }
            case 0x77: {  // dneg
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofLong(bitCast<int64_t>(-bitCast<double>(v.value_or(0)))));
                ++pc; break;
            }

            case 0x62: case 0x63: case 0x66: case 0x67:
            case 0x6a: case 0x6b: case 0x6e: case 0x6f:
            case 0x72: case 0x73: {
                Value rhsV = frame.pop(), lhsV = frame.pop();
                if (op & 1) {  // double ops: 0x63 dadd, 0x67 dsub, 0x6b dmul, 0x6f ddiv, 0x73 drem
                    auto r = parseLongValue(rhsV), l = parseLongValue(lhsV);
                    if (l && r) {
                        double a = bitCast<double>(*l), b = bitCast<double>(*r), res;
                        switch (op) {
                            case 0x63: res = a + b; break;
                            case 0x67: res = a - b; break;
                            case 0x6b: res = a * b; break;
                            case 0x6f: res = a / b; break;
                            case 0x73: res = std::fmod(a, b); break;
                            default:   res = 0.0; break;
                        }
                        frame.push(Value::ofLong(bitCast<int64_t>(res)));
                    } else { frame.push(Value::named("<darith>")); }
                } else {  // float ops: 0x62 fadd, 0x66 fsub, 0x6a fmul, 0x6e fdiv, 0x72 frem
                    auto r = parseIntValue(rhsV), l = parseIntValue(lhsV);
                    if (l && r) {
                        float a = bitCast<float>(*l), b = bitCast<float>(*r), res;
                        switch (op) {
                            case 0x62: res = a + b; break;
                            case 0x66: res = a - b; break;
                            case 0x6a: res = a * b; break;
                            case 0x6e: res = a / b; break;
                            case 0x72: res = std::fmod(a, b); break;
                            default:   res = 0.0f; break;
                        }
                        frame.push(Value::ofInt(bitCast<int32_t>(res)));
                    } else { frame.push(Value::named("<farith>")); }
                }
                ++pc; break;
            }

            case 0x78:
            case 0x7a:
            case 0x7c:
            case 0x7e:
            case 0x80:
            case 0x82: {
                const uint32_t t0arith = statNow();
                Value rhsValue = frame.pop();
                Value lhsValue = frame.pop();
                std::optional<int> rhs = parseIntValue(rhsValue);
                std::optional<int> lhs = parseIntValue(lhsValue);
                if (lhs.has_value() && rhs.has_value()) {
                    switch (op) {
                        case 0x78: frame.push(Value::ofInt(*lhs << (*rhs & 0x1f))); break;
                        case 0x7a: frame.push(Value::ofInt(*lhs >> (*rhs & 0x1f))); break;
                        case 0x7c: frame.push(Value::ofInt(static_cast<int32_t>(static_cast<uint32_t>(*lhs) >> (*rhs & 0x1f)))); break;
                        case 0x7e: frame.push(Value::ofInt(*lhs & *rhs)); break;
                        case 0x80: frame.push(Value::ofInt(*lhs | *rhs)); break;
                        case 0x82: frame.push(Value::ofInt(*lhs ^ *rhs)); break;
                    }
                } else {
                    frame.push(Value::named("<int-bitop>"));
                }
                if(t0arith) rt.host->arithStats.record(nowUs() - t0arith);
                ++pc;
                break;
            }

            case 0x79:
            case 0x7b:
            case 0x7d: {
                const uint32_t t0arith = statNow();
                Value rhsValue = frame.pop();
                Value lhsValue = frame.pop();
                std::optional<int> rhs = parseIntValue(rhsValue);
                std::optional<long long> lhs = parseLongValue(lhsValue);
                if (lhs.has_value() && rhs.has_value()) {
                    switch (op) {
                        case 0x79: frame.push(Value::ofLong(*lhs << (*rhs & 0x3f))); break;
                        case 0x7b: frame.push(Value::ofLong(*lhs >> (*rhs & 0x3f))); break;
                        case 0x7d: frame.push(Value::ofLong(static_cast<int64_t>(static_cast<uint64_t>(*lhs) >> (*rhs & 0x3f)))); break;
                    }
                } else {
                    frame.push(Value::named("<long-shift>"));
                }
                if(t0arith) rt.host->arithStats.record(nowUs() - t0arith);
                ++pc;
                break;
            }

            case 0x7f:
            case 0x81:
            case 0x83: {
                const uint32_t t0arith = statNow();
                Value rhsValue = frame.pop();
                Value lhsValue = frame.pop();
                std::optional<long long> rhs = parseLongValue(rhsValue);
                std::optional<long long> lhs = parseLongValue(lhsValue);
                if (lhs.has_value() && rhs.has_value()) {
                    switch (op) {
                        case 0x7f: frame.push(Value::ofLong(*lhs & *rhs)); break;
                        case 0x81: frame.push(Value::ofLong(*lhs | *rhs)); break;
                        case 0x83: frame.push(Value::ofLong(*lhs ^ *rhs)); break;
                    }
                } else {
                    frame.push(Value::named("<long-bitop>"));
                }
                if(t0arith) rt.host->arithStats.record(nowUs() - t0arith);
                ++pc;
                break;
            }

            case 0x85: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value() ? Value::ofLong(*parsed) : Value::named("<i2l:" + value.asText() + ">"));
                ++pc;
                break;
            }

            case 0x88: {
                Value value = frame.pop();
                std::optional<long long> parsed = parseLongValue(value);
                frame.push(parsed.has_value() ? Value::ofInt(static_cast<int32_t>(*parsed)) : Value::named("<l2i:" + value.asText() + ">"));
                ++pc;
                break;
            }

            case 0x86: {  // i2f
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofInt(bitCast<int32_t>(v ? static_cast<float>(*v) : 0.0f)));
                ++pc; break;
            }
            case 0x87: {  // i2d
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofLong(bitCast<int64_t>(v ? static_cast<double>(*v) : 0.0)));
                ++pc; break;
            }
            case 0x89: {  // l2f
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofInt(bitCast<int32_t>(v ? static_cast<float>(*v) : 0.0f)));
                ++pc; break;
            }
            case 0x8a: {  // l2d
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofLong(bitCast<int64_t>(v ? static_cast<double>(*v) : 0.0)));
                ++pc; break;
            }
            case 0x8b: {  // f2i
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofInt(v ? static_cast<int32_t>(bitCast<float>(*v)) : 0));
                ++pc; break;
            }
            case 0x8c: {  // f2l
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofLong(v ? static_cast<int64_t>(bitCast<float>(*v)) : 0));
                ++pc; break;
            }
            case 0x8d: {  // f2d
                auto v = parseIntValue(frame.pop());
                frame.push(Value::ofLong(bitCast<int64_t>(v ? static_cast<double>(bitCast<float>(*v)) : 0.0)));
                ++pc; break;
            }
            case 0x8e: {  // d2i
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofInt(v ? static_cast<int32_t>(bitCast<double>(*v)) : 0));
                ++pc; break;
            }
            case 0x8f: {  // d2l
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofLong(v ? static_cast<int64_t>(bitCast<double>(*v)) : 0));
                ++pc; break;
            }
            case 0x90: {  // d2f
                auto v = parseLongValue(frame.pop());
                frame.push(Value::ofInt(bitCast<int32_t>(v ? static_cast<float>(bitCast<double>(*v)) : 0.0f)));
                ++pc; break;
            }

            case 0x91: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value() ? Value::ofInt(static_cast<int8_t>(*parsed)) : value);
                ++pc;
                break;
            }

            case 0x92: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value() ? Value::ofInt(static_cast<uint16_t>(*parsed)) : value);
                ++pc;
                break;
            }

            case 0x93: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value() ? Value::ofInt(static_cast<int16_t>(*parsed)) : value);
                ++pc;
                break;
            }

            case 0x94: {
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                std::optional<long long> left = parseLongValue(lhs);
                std::optional<long long> right = parseLongValue(rhs);
                frame.push(left.has_value() && right.has_value()
                    ? Value::ofInt(*left < *right ? -1 : *left > *right ? 1 : 0)
                    : Value::named("<lcmp:" + lhs.asText() + "," + rhs.asText() + ">"));
                ++pc;
                break;
            }

            case 0x95: case 0x96: case 0x97: case 0x98: {  // fcmpl fcmpg dcmpl dcmpg
                Value rhsV = frame.pop(), lhsV = frame.pop();
                int32_t cmp;
                if (op >= 0x97) {  // dcmpl / dcmpg
                    auto r = parseLongValue(rhsV), l = parseLongValue(lhsV);
                    if (l && r) {
                        double a = bitCast<double>(*l), b = bitCast<double>(*r);
                        cmp = (std::isnan(a) || std::isnan(b)) ? (op == 0x97 ? -1 : 1)
                                                                : (a < b ? -1 : a > b ? 1 : 0);
                    } else { cmp = 0; }
                } else {  // fcmpl / fcmpg
                    auto r = parseIntValue(rhsV), l = parseIntValue(lhsV);
                    if (l && r) {
                        float a = bitCast<float>(*l), b = bitCast<float>(*r);
                        cmp = (std::isnan(a) || std::isnan(b)) ? (op == 0x95 ? -1 : 1)
                                                                : (a < b ? -1 : a > b ? 1 : 0);
                    } else { cmp = 0; }
                }
                frame.push(Value::ofInt(cmp));
                ++pc; break;
            }

            case 0x99:
            case 0x9a:
            case 0x9b:
            case 0x9c:
            case 0x9d:
            case 0x9e: {
                const uint32_t t0b=statNow();
                int16_t offset = codeS2(code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                bool taken = parsed.has_value() && compareIntWithZero(*parsed, op);
                if (rt.trace.recording) {
                    rt.trace.branches.push_back(BranchTrace{
                        label,
                        static_cast<uint32_t>(pc),
                        compareText(value, compareZeroOpText(op), Value::named("0")),
                        parsed.has_value(),
                        taken,
                        target,
                    });
                }
                pc = taken ? target : pc + 3;
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0x9f:
            case 0xa0:
            case 0xa1:
            case 0xa2:
            case 0xa3:
            case 0xa4: {
                const uint32_t t0b=statNow();
                int16_t offset = codeS2(code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                std::optional<int> left = parseIntValue(lhs);
                std::optional<int> right = parseIntValue(rhs);
                bool taken = left.has_value() && right.has_value() && compareInts(*left, *right, op);
                if (rt.trace.recording) {
                    rt.trace.branches.push_back(BranchTrace{
                        label,
                        static_cast<uint32_t>(pc),
                        compareText(lhs, compareOpText(op), rhs),
                        left.has_value() && right.has_value(),
                        taken,
                        target,
                    });
                }
                pc = taken ? target : pc + 3;
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0xa5:
            case 0xa6: {  // if_acmpeq / if_acmpne
                int16_t offset = codeS2(code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value rhs = frame.pop(), lhs = frame.pop();
                bool equal = false;
                if (lhs.tag == rhs.tag) {
                    if (lhs.tag == Value::Tag::kInt)  equal = (lhs.i32 == rhs.i32);
                    else if (lhs.tag == Value::Tag::kStr) equal = (*lhs.str == *rhs.str);
                }
                pc = ((op == 0xa5) ? equal : !equal) ? target : pc + 3;
                break;
            }

            case 0xa7: {
                const uint32_t t0b=statNow();
                pc = branchTarget(pc, codeS2(code, pc + 1));
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0xaa: {
                const uint32_t t0b=statNow();
                Value keyValue = frame.pop();
                const int32_t key = parseIntValue(keyValue).value_or(0);
                size_t table = pc + 1;
                while ((table & 3u) != 0u) {
                    ++table;
                }
                const int32_t defaultOffset = codeS4(code, table);
                const int32_t low = codeS4(code, table + 4);
                const int32_t high = codeS4(code, table + 8);
                int32_t offset = defaultOffset;
                if (key >= low && key <= high) {
                    const size_t index = static_cast<size_t>(key - low);
                    offset = codeS4(code, table + 12 + index * 4);
                }
                const uint32_t target = branchTarget(pc, offset);
                if (rt.trace.recording) {
                    rt.trace.branches.push_back(BranchTrace{
                        label,
                        static_cast<uint32_t>(pc),
                        "tableswitch " + keyValue.asText(),
                        true,
                        offset != defaultOffset || (key >= low && key <= high),
                        target,
                    });
                }
                pc = target;
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0xab: {
                const uint32_t t0b=statNow();
                Value keyValue = frame.pop();
                const int32_t key = parseIntValue(keyValue).value_or(0);
                size_t table = pc + 1;
                while ((table & 3u) != 0u) {
                    ++table;
                }
                const int32_t defaultOffset = codeS4(code, table);
                const int32_t pairs = codeS4(code, table + 4);
                const size_t pairsStart = table + 8;
                int32_t offset = defaultOffset;
                bool matched = false;
                int32_t lo = 0;
                int32_t hi = pairs - 1;
                while (lo <= hi) {
                    const int32_t mid = lo + ((hi - lo) >> 1);
                    const size_t pair = pairsStart + static_cast<size_t>(mid) * 8;
                    const int32_t match = codeS4(code, pair);
                    if (key < match) {
                        hi = mid - 1;
                    } else if (key > match) {
                        lo = mid + 1;
                    } else {
                        offset = codeS4(code, pair + 4);
                        matched = true;
                        break;
                    }
                }
                const uint32_t target = branchTarget(pc, offset);
                if (rt.trace.recording) {
                    rt.trace.branches.push_back(BranchTrace{
                        label,
                        static_cast<uint32_t>(pc),
                        "lookupswitch " + keyValue.asText(),
                        true,
                        matched,
                        target,
                    });
                }
                pc = target;
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0xc6:
            case 0xc7: {
                const uint32_t t0b=statNow();
                int16_t offset = codeS2(code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value value = frame.pop();
                bool taken = op == 0xc6 ? value.isNull() : !value.isNull();
                if (rt.trace.recording) {
                    rt.trace.branches.push_back(BranchTrace{
                        label,
                        static_cast<uint32_t>(pc),
                        value.asText() + (op == 0xc6 ? " == null" : " != null"),
                        true,
                        taken,
                        target,
                    });
                }
                pc = taken ? target : pc + 3;
                if(t0b) rt.host->branchStats.record(nowUs()-t0b);
                break;
            }

            case 0xb2:
            {
                const uint32_t t0m=statNow();
                const uint16_t cpIdx = codeU2(code, pc + 1);
                const uint64_t skey = callCacheKey(&cls, cpIdx);
                auto skit = rt.staticKeyCache.find(skey);
                const std::string& key = (skit != rt.staticKeyCache.end())
                    ? skit->second
                    : [&]() -> const std::string& {
                        FieldRef ref = resolveFieldRef(cls, cpIdx);
                        return rt.staticKeyCache.emplace(skey, ref.className + "." + ref.name + "|" + ref.descriptor).first->second;
                    }();
                ensureClassInitialized(classes, key.substr(0, key.find('.')), rt);
                auto it = rt.staticFields.find(key);
                frame.push(it == rt.staticFields.end() ? Value::ofInt(0) : it->second);
                pc += 3;
                if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                break;
            }

            case 0xb3:
            {
                uint32_t writePc = static_cast<uint32_t>(pc);
                const uint16_t cpIdx = codeU2(code, pc + 1);
                const uint64_t skey = callCacheKey(&cls, cpIdx);
                auto skit = rt.staticKeyCache.find(skey);
                const std::string& key = (skit != rt.staticKeyCache.end())
                    ? skit->second
                    : [&]() -> const std::string& {
                        FieldRef ref = resolveFieldRef(cls, cpIdx);
                        return rt.staticKeyCache.emplace(skey, ref.className + "." + ref.name + "|" + ref.descriptor).first->second;
                    }();
                ensureClassInitialized(classes, key.substr(0, key.find('.')), rt);
                Value value = frame.pop();
                rt.staticFields[key] = value;
                if (rt.trace.recording) rt.trace.staticWrites.push_back(StaticWrite{label, writePc, key, value});
                pc += 3;
                break;
            }

            case 0xb4:
            {
                const uint16_t cpIdx = codeU2(code, pc + 1);
                Value object = frame.pop();
                std::optional<uint32_t> id = objectId(object);
                Value value = Value::ofInt(0);
                const uint32_t t0 = statNow();
                if (id.has_value()) {
                    auto objectIt = rt.heap.find(*id);
                    if (objectIt != rt.heap.end()) {
                        HeapObject& obj = objectIt->second;
                        // Key by executing class (not obj.cls) so cpIdx is interpreted in the
                        // correct constant pool — avoids cross-class collisions when two classes
                        // share the same cpIdx for different fields on the same object type.
                        const uint64_t fidxKey = callCacheKey(&cls, cpIdx);
                        auto fidxIt = rt.fieldIndexCache.find(fidxKey);
                        if (fidxIt != rt.fieldIndexCache.end()) {
                            // Hot path: integer-keyed SRAM lookup → direct vector index.
                            const uint16_t slot = fidxIt->second;
                            if (slot < obj.fields.size()) {
                                const Value& sv = obj.fields[slot];
                                if (sv.isInitialized()) value = sv;
                            }
                        } else {
                            // Cold path: resolve name|descriptor key, find slot, populate cache.
                            const std::string& fieldKey = resolveFieldKey(rt, cls, cpIdx);
                            if (obj.cls != nullptr) {
                                buildFieldSlots(rt, classes, *obj.cls);
                                const auto& slotMap = rt.fieldSlotCache[obj.cls];
                                auto nameIt = slotMap.find(fieldKey);
                                if (nameIt != slotMap.end()) {
                                    const uint16_t slot = nameIt->second;
                                    rt.fieldIndexCache[fidxKey] = slot;
                                    if (slot < obj.fields.size()) {
                                        const Value& sv = obj.fields[slot];
                                        if (sv.isInitialized()) value = sv;
                                    }
                                }
                            }
                        }
                    }
                }
                if(t0) rt.host->getfieldStats.record(nowUs() - t0);
                frame.push(value);
                pc += 3;
                break;
            }

            case 0xb5:
            {
                uint32_t writePc = static_cast<uint32_t>(pc);
                const uint16_t cpIdx = codeU2(code, pc + 1);
                Value value = frame.pop();
                Value object = frame.pop();
                std::optional<uint32_t> id = objectId(object);
                const uint32_t t0b5 = statNow();
                if (id.has_value()) {
                    HeapObject& obj = rt.heap[*id];
                    const uint64_t fidxKey = callCacheKey(&cls, cpIdx);
                    auto fidxIt = rt.fieldIndexCache.find(fidxKey);
                    if (fidxIt != rt.fieldIndexCache.end()) {
                        // Hot path: integer-keyed SRAM lookup → direct vector index.
                        const uint16_t slot = fidxIt->second;
                        if (slot >= obj.fields.size()) obj.fields.resize(slot + 1);
                        obj.fields[slot] = value;
                    } else {
                        // Cold path: resolve name|descriptor key, find slot, populate cache.
                        const std::string& fieldKey = resolveFieldKey(rt, cls, cpIdx);
                        if (obj.cls != nullptr) {
                            buildFieldSlots(rt, classes, *obj.cls);
                            const auto& slotMap = rt.fieldSlotCache[obj.cls];
                            auto nameIt = slotMap.find(fieldKey);
                            if (nameIt != slotMap.end()) {
                                const uint16_t slot = nameIt->second;
                                rt.fieldIndexCache[fidxKey] = slot;
                                if (slot >= obj.fields.size()) obj.fields.resize(slot + 1);
                                obj.fields[slot] = value;
                            }
                        }
                    }
                }
                if(t0b5) rt.host->putfieldStats.record(nowUs() - t0b5);
                if (rt.trace.recording) {
                    const std::string& fieldName = resolveFieldName(rt, cls, cpIdx);
                    FieldRef ref = resolveFieldRef(cls, cpIdx);
                    rt.trace.fieldWrites.push_back(FieldWrite{label, writePc, object, ref.className + "." + fieldName, value});
                }
                pc += 3;
                break;
            }

            case 0xb8: {
                const uint32_t t0inv = statNow();
                uint32_t callPc = static_cast<uint32_t>(pc);
                const uint16_t cpIdx = codeU2(code, pc + 1);

                // Per-class call-site cache lookup (fast path). siteEntry may
                // be nullptr if cpIdx is out-of-range or marked 0xFFFF
                // (defensive — well-formed bytecode never does this).
                CallSiteEntry* siteEntry = nullptr;
                if (cpIdx < cls.cpToSite.size()) {
                    const uint16_t siteIdx = cls.cpToSite[cpIdx];
                    if (siteIdx != 0xFFFF) {
                        siteEntry = &cls.sites[siteIdx];
                    }
                }

                // FAST PATH: kNative + non-null leaf. Skips callCache hash
                // lookup AND MethodRef construction. Inert until step 3 wires
                // resolveStaticLeaf to return real leaves.
                if (siteEntry != nullptr
                    && siteEntry->kind == CallSiteEntry::kNative
                    && siteEntry->nativeFn != nullptr) {
                    const NativeLeafFn leaf = siteEntry->nativeFn;
                    const uint8_t fastArgSlots = siteEntry->argSlots;
                    const ClassFile* fastTargetClass = siteEntry->targetClass;
                    const MethodInfo* fastTargetMethod = siteEntry->targetMethod;

                    if (fastTargetClass != nullptr) {
                        ensureClassInitialized(classes, fastTargetClass->thisClass, rt);
                    }

                    rt.callArgsBuf.resize(fastArgSlots);
                    for (size_t i = fastArgSlots; i > 0; --i) {
                        rt.callArgsBuf[i - 1] = frame.pop();
                    }
                    sharedNativeCtx.receiverClassName = {};

                    const uint32_t tN =
#if JVM_ENABLE_NATIVE_PROFILING
                        (rt.host && rt.host->profileNatives) ? nowUs() : 0;
#else
                        0;
#endif
                    const uint32_t tTaskNative =
                        (rt.host && rt.host->profileTaskMethods && rt.currentTask != nullptr) ? nowUs() : 0;

                    NativeCallResult nativeResult;
                    try {
                        nativeResult = leaf(sharedNativeCtx, callPc, rt.callArgsBuf);
                    } catch (const YieldThreadSleep&) {
                        if (tTaskNative != 0 && fastTargetClass && fastTargetMethod) {
                            MethodRef profRef{fastTargetClass->thisClass, fastTargetMethod->name, fastTargetMethod->descriptor};
                            recordTaskNativeProfile(rt, profRef, nowUs() - tTaskNative);
                        }
                        pc += 3;
                        runtimeFrame.pc = pc;
                        rt.pendingYieldRethrowStartUs =
                            (rt.host && rt.host->profileTaskMethods) ? nowUs() : 0;
                        throw;
                    }
                    if (tTaskNative != 0 && fastTargetClass && fastTargetMethod) {
                        MethodRef profRef{fastTargetClass->thisClass, fastTargetMethod->name, fastTargetMethod->descriptor};
                        recordTaskNativeProfile(rt, profRef, nowUs() - tTaskNative);
                    }
#if JVM_ENABLE_NATIVE_PROFILING
                    if (tN && fastTargetClass && fastTargetMethod) {
                        rt.host->nativeStats[fastTargetClass->thisClass + "." + fastTargetMethod->name].record(nowUs() - tN);
                    }
#endif
                    if (rt.yieldRequested) {
                        pc += 3;
                        runtimeFrame.pc = pc;
                        return std::nullopt;
                    }
                    if (nativeResult.exception.has_value()) {
                        setPendingException(rt, *nativeResult.exception, label, callPc);
                    }
                    if (rt.pendingException.has_value()) {
                        if (catchPendingException(callPc)) {
                            if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                            break;
                        }
                        runtimeFrame.pc = callPc;
                        if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                        return finish(std::nullopt);
                    }
                    if (nativeResult.handled) {
                        if (nativeResult.returnValue.has_value()) {
                            frame.push(*nativeResult.returnValue);
                        }
                    } else {
                        MethodRef ref{fastTargetClass->thisClass, fastTargetMethod->name, fastTargetMethod->descriptor};
                        std::optional<Value> result = recordUnknownCall(
                            rt, label, callPc, ref, rt.callArgsBuf);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                    pc += 3;
                    if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                    break;
                }

                const uint64_t ckey = callCacheKey(&cls, cpIdx);

                const ClassFile* targetClass = nullptr;
                const MethodInfo* targetMethod = nullptr;
                size_t argSlots = 0;
                bool isNativeCall = false;
                MethodRef ref;
                bool haveRef = false;

                NativeHandler cachedNativeHandler = nullptr;
                auto cit = rt.callCache.find(ckey);
                if (cit != rt.callCache.end()) {
                    argSlots = cit->second.argSlots;
                    targetClass = cit->second.targetClass;
                    targetMethod = cit->second.method;
                    isNativeCall = cit->second.isNative;
                    cachedNativeHandler = cit->second.nativeHandler;
                } else {
                    ref = resolveMethodRef(cls, cpIdx);
                    haveRef = true;
                    argSlots = argumentSlotWidths(ref.descriptor).size();
                    targetMethod = findMethodInHierarchy(
                        classes, ref.className, ref.name, ref.descriptor, &targetClass);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        isNativeCall = hasAccess(targetMethod->access, kAccNative);
                        cachedNativeHandler = isNativeCall
                            ? resolveNativeStaticHandler(targetClass->thisClass)
                            : nullptr;
                        rt.callCache[ckey] = {static_cast<uint8_t>(argSlots), isNativeCall, "", nullptr, targetClass, targetMethod, cachedNativeHandler};
                    }
                }

                // Populate per-class call-site entry on first native resolve.
                // For non-native calls we leave the site kUncached for now;
                // Java fast path is wired in step 4.
                if (siteEntry != nullptr
                    && siteEntry->kind == CallSiteEntry::kUncached
                    && targetClass != nullptr && targetMethod != nullptr
                    && isNativeCall) {
                    if (!haveRef) ref = resolveMethodRef(cls, cpIdx);
                    NativeLeafFn leaf = resolveStaticLeaf(
                        targetClass->thisClass, targetMethod->name, targetMethod->descriptor);
                    if (leaf != nullptr) {
                        siteEntry->kind = CallSiteEntry::kNative;
                        siteEntry->nativeFn = leaf;
                        siteEntry->argSlots = static_cast<uint8_t>(argSlots);
                        siteEntry->targetClass = targetClass;
                        siteEntry->targetMethod = targetMethod;
                    } else {
                        siteEntry->kind = CallSiteEntry::kSlowCascade;
                    }
                }

                if (targetClass != nullptr) {
                    ensureClassInitialized(classes, targetClass->thisClass, rt);
                } else if (haveRef) {
                    ensureClassInitialized(classes, ref.className, rt);
                }

                rt.callArgsBuf.resize(argSlots);
                for (size_t i = argSlots; i > 0; --i) {
                    rt.callArgsBuf[i - 1] = frame.pop();
                }

                if (targetClass != nullptr && targetMethod != nullptr) {
                    if (isNativeCall) {
                        sharedNativeCtx.receiverClassName = {};
                        MethodRef nativeRef{targetClass->thisClass, targetMethod->name, targetMethod->descriptor};
                        NativeCallResult nativeResult;
                        const uint32_t tN =
#if JVM_ENABLE_NATIVE_PROFILING
                            (rt.host && rt.host->profileNatives) ? nowUs() : 0;
#else
                            0;
#endif
                        const uint32_t tTaskNative =
                            (rt.host && rt.host->profileTaskMethods && rt.currentTask != nullptr) ? nowUs() : 0;
                        try {
                            nativeResult = cachedNativeHandler != nullptr
                                ? cachedNativeHandler(sharedNativeCtx, label, callPc, nativeRef, rt.callArgsBuf)
                                : handleNativeStaticCall(sharedNativeCtx, label, callPc, nativeRef, rt.callArgsBuf);
                        } catch (const YieldThreadSleep&) {
                            if (tTaskNative != 0) {
                                recordTaskNativeProfile(rt, nativeRef, nowUs() - tTaskNative);
                            }
                            pc += 3;
                            runtimeFrame.pc = pc;
                            rt.pendingYieldRethrowStartUs =
                                (rt.host && rt.host->profileTaskMethods) ? nowUs() : 0;
                            throw;
                        }
                        if (tTaskNative != 0) {
                            recordTaskNativeProfile(rt, nativeRef, nowUs() - tTaskNative);
                        }
#if JVM_ENABLE_NATIVE_PROFILING
                        if (tN) rt.host->nativeStats[nativeRef.className + "." + nativeRef.name].record(nowUs() - tN);
#endif
                        if (rt.yieldRequested) {
                            pc += 3;
                            runtimeFrame.pc = pc;
                            return std::nullopt;
                        }
                        if (nativeResult.exception.has_value()) {
                            setPendingException(rt, *nativeResult.exception, label, callPc);
                        }
                        if (rt.pendingException.has_value()) {
                            if (catchPendingException(callPc)) {
                                if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                                break;
                            }
                            runtimeFrame.pc = callPc;
                            if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                            return finish(std::nullopt);
                        }
                        if (nativeResult.handled) {
                            if (nativeResult.returnValue.has_value()) {
                                frame.push(*nativeResult.returnValue);
                            }
                        } else {
                            std::optional<Value> result = recordUnknownCall(
                                rt, label, callPc, nativeRef, rt.callArgsBuf);
                            if (result.has_value()) {
                                frame.push(*result);
                            }
                        }
                    } else {
                        // Trampoline: save continuation pc + invoke pc, push the
                        // callee frame, and return to the outer trampoline loop.
                        // It will re-enter this frame after the callee returns
                        // (or throws — handled by the entry guard above).
                        runtimeFrame.lastCallPc = callPc;
                        runtimeFrame.pc = pc + 3;
                        const uint32_t tDisp0 = t0inv ? nowUs() - t0inv : 0;
                        if(t0inv) rt.host->invokeStats.record(tDisp0);
                        if (!pushJavaFrame(rt, classes, *targetClass, *targetMethod, rt.callArgsBuf)) {
                            // StackOverflowError raised by pushJavaFrame
                            if (catchPendingException(callPc)) {
                                runtimeFrame.lastCallPc = SIZE_MAX;
                                break;
                            }
                            runtimeFrame.pc = callPc;
                            return finish(std::nullopt);
                        }
                        return std::nullopt;
                    }
                } else {
                    if (!haveRef) ref = resolveMethodRef(cls, cpIdx);
                    std::optional<Value> result = recordUnknownCall(rt, label, callPc, ref, rt.callArgsBuf);
                    if (result.has_value()) {
                        frame.push(*result);
                    }
                }
                pc += 3;
                if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                break;
            }

            case 0xb6:
            case 0xb7:
            case 0xb9: {
                const uint32_t t0inv = statNow();
                const uint16_t cpIdx = codeU2(code, pc + 1);
                const bool isVirtualOp = (op == 0xb6 || op == 0xb9);
                const uint64_t ckey = callCacheKey(&cls, cpIdx);

                const ClassFile* targetClass = nullptr;
                const MethodInfo* targetMethod = nullptr;
                bool isNativeCall = false;
                size_t argSlots = 0;
                MethodRef ref;
                bool haveRef = false;

                // Phase 1: get argSlots (must happen before popping stack)
                auto cit = rt.callCache.find(ckey);
                if (cit != rt.callCache.end()) {
                    argSlots = cit->second.argSlots;
                } else {
                    ref = resolveMethodRef(cls, cpIdx);
                    haveRef = true;
                    argSlots = argumentSlotWidths(ref.descriptor).size();
                }

                // Phase 2: pop args + this
                rt.callArgsBuf.resize(argSlots + 1);
                for (size_t i = argSlots; i > 0; --i) {
                    rt.callArgsBuf[i] = frame.pop();
                }
                rt.callArgsBuf[0] = frame.pop(); // 'this'
                const Value& object = rt.callArgsBuf[0]; // ref into callArgsBuf (no copy)

                if (object.isNull()) {
                    const uint32_t npe_pc = static_cast<uint32_t>(pc);
                    setPendingException(rt,
                        allocateObject(rt, classes, label, npe_pc, "java/lang/NullPointerException"),
                        label, npe_pc);
                    if (catchPendingException(npe_pc)) {
                        if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                        break;
                    }
                    runtimeFrame.pc = npe_pc;
                    if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                    return finish(std::nullopt);
                }

                // Phase 3: determine lookupClassName + lookupClassPtr for cache hit check
                std::string lookupClassName = haveRef ? ref.className : "";
                const ClassFile* lookupClassPtr = nullptr;
                bool lookupClassFromCache = false;
                if (isVirtualOp) {
                    std::optional<uint32_t> id = objectId(object);
                    if (id.has_value()) {
                        auto objectIt = rt.heap.find(*id);
                        if (objectIt != rt.heap.end()) {
                            lookupClassName = objectIt->second.className;
                            lookupClassPtr = objectIt->second.cls;
                        } else if (!haveRef) {
                            lookupClassName = cit->second.runtimeClass;
                            lookupClassFromCache = true;
                        }
                    } else if (!haveRef) {
                        ref = resolveMethodRef(cls, cpIdx);
                        haveRef = true;
                        lookupClassName = ref.className;
                    }
                } else if (!haveRef) {
                    // invokespecial on cache hit: use stored ref.className
                    lookupClassName = cit->second.runtimeClass;
                    lookupClassFromCache = true;
                }

                // Phase 4: resolve dispatch (cache hit when runtime class matches).
                // Use pointer comparison when both sides are set (faster than string compare).
                const bool cacheClassMatch = cit != rt.callCache.end() && (
                    lookupClassFromCache ||
                    (lookupClassPtr && cit->second.runtimeClassPtr == lookupClassPtr) ||
                    (!lookupClassPtr && cit->second.runtimeClass == lookupClassName));
                NativeHandler cachedNativeHandler = nullptr;
                if (cacheClassMatch) {
                    targetClass = cit->second.targetClass;
                    targetMethod = cit->second.method;
                    isNativeCall = cit->second.isNative;
                    cachedNativeHandler = cit->second.nativeHandler;
                } else {
                    const std::string& lookupName = haveRef ? ref.name : cit->second.method->name;
                    const std::string& lookupDesc = haveRef ? ref.descriptor : cit->second.method->descriptor;
                    targetMethod = findMethodInHierarchy(
                        classes, lookupClassName, lookupName, lookupDesc, &targetClass);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        isNativeCall = hasAccess(targetMethod->access, kAccNative);
                        cachedNativeHandler = isNativeCall
                            ? resolveNativeInstanceHandler(targetClass->thisClass)
                            : nullptr;
                        rt.callCache[ckey] = {static_cast<uint8_t>(argSlots), isNativeCall,
                                              lookupClassName, findClass(classes, lookupClassName),
                                              targetClass, targetMethod, cachedNativeHandler};
                    }
                }

                if (targetClass != nullptr && targetMethod != nullptr) {
                    if (isNativeCall) {
                        sharedNativeCtx.receiverClassName = {};
                        std::optional<uint32_t> nativeObjectId = objectId(object);
                        if (nativeObjectId.has_value()) {
                            auto objectIt = rt.heap.find(*nativeObjectId);
                            if (objectIt != rt.heap.end()) {
                                sharedNativeCtx.receiverClassName = objectIt->second.className;
                            }
                        }
                        MethodRef nativeRef{targetClass->thisClass, targetMethod->name, targetMethod->descriptor};
                        NativeCallResult nativeResult;
                        const uint32_t tN =
#if JVM_ENABLE_NATIVE_PROFILING
                            (rt.host && rt.host->profileNatives) ? nowUs() : 0;
#else
                            0;
#endif
                        const uint32_t tTaskNative =
                            (rt.host && rt.host->profileTaskMethods && rt.currentTask != nullptr) ? nowUs() : 0;
                        sharedNativeCtx.tProfT0 = tN;
                        sharedNativeCtx.tProfTEntry = 0;
                        try {
                            nativeResult = cachedNativeHandler != nullptr
                                ? cachedNativeHandler(sharedNativeCtx, label, static_cast<uint32_t>(pc), nativeRef, rt.callArgsBuf)
                                : handleNativeInstanceCall(sharedNativeCtx, label, static_cast<uint32_t>(pc), nativeRef, rt.callArgsBuf);
                        } catch (const YieldThreadSleep&) {
                            if (tTaskNative != 0) {
                                recordTaskNativeProfile(rt, nativeRef, nowUs() - tTaskNative);
                            }
                            pc += op == 0xb9 ? 5 : 3;
                            runtimeFrame.pc = pc;
                            rt.pendingYieldRethrowStartUs =
                                (rt.host && rt.host->profileTaskMethods) ? nowUs() : 0;
                            throw;
                        }
                        if (tTaskNative != 0) {
                            recordTaskNativeProfile(rt, nativeRef, nowUs() - tTaskNative);
                        }
#if JVM_ENABLE_NATIVE_PROFILING
                        if (tN) rt.host->nativeStats[nativeRef.className + "." + nativeRef.name].record(nowUs() - tN);
#endif
                        if (rt.yieldRequested) {
                            pc += op == 0xb9 ? 5 : 3;
                            runtimeFrame.pc = pc;
                            return std::nullopt;
                        }
                        if (nativeResult.exception.has_value()) {
                            setPendingException(rt, *nativeResult.exception, label, static_cast<uint32_t>(pc));
                        }
                        if (rt.pendingException.has_value()) {
                            if (catchPendingException(static_cast<uint32_t>(pc))) {
                                if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                                break;
                            }
                            runtimeFrame.pc = pc;
                            if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                            return finish(std::nullopt);
                        }
                        if (nativeResult.handled) {
                            if (nativeResult.returnValue.has_value()) {
                                frame.push(*nativeResult.returnValue);
                            }
                        } else {
                            std::optional<Value> result = recordUnknownCall(
                                rt, label, static_cast<uint32_t>(pc), nativeRef, rt.callArgsBuf);
                            if (result.has_value()) {
                                frame.push(*result);
                            }
                        }
                    } else {
                        // Trampoline: save continuation pc + invoke pc, push the
                        // callee frame, and return to the outer trampoline loop.
                        const size_t invokePc = pc;
                        runtimeFrame.lastCallPc = invokePc;
                        runtimeFrame.pc = pc + invokeLength(op);
                        const uint32_t tDisp0 = t0inv ? nowUs() - t0inv : 0;
                        if(t0inv) rt.host->invokeStats.record(tDisp0);
                        if (!pushJavaFrame(rt, classes, *targetClass, *targetMethod, rt.callArgsBuf)) {
                            // StackOverflowError raised by pushJavaFrame
                            if (catchPendingException(static_cast<uint32_t>(invokePc))) {
                                runtimeFrame.lastCallPc = SIZE_MAX;
                                break;
                            }
                            runtimeFrame.pc = invokePc;
                            return finish(std::nullopt);
                        }
                        return std::nullopt;
                    }
                } else {
                    if (!haveRef) ref = resolveMethodRef(cls, cpIdx);
                    NativeCallResult builtInResult = handleBuiltInInstanceCall(rt, classes, ref, rt.callArgsBuf);
                    if (builtInResult.handled) {
                        if (builtInResult.exception.has_value()) {
                            setPendingException(rt, *builtInResult.exception, label, static_cast<uint32_t>(pc));
                        }
                        if (rt.pendingException.has_value()) {
                            if (catchPendingException(static_cast<uint32_t>(pc))) {
                                if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                                break;
                            }
                            runtimeFrame.pc = pc;
                            if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                            return finish(std::nullopt);
                        }
                        if (builtInResult.returnValue.has_value()) {
                            frame.push(*builtInResult.returnValue);
                        }
                    } else {
                        std::optional<Value> result = recordUnknownCall(
                            rt, label, static_cast<uint32_t>(pc), ref, rt.callArgsBuf);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                }
                pc += op == 0xb9 ? 5 : 3;
                if(t0inv) rt.host->invokeStats.record(nowUs() - t0inv);
                break;
            }

            case 0xbb:
            {
                const uint32_t t0m=statNow();
                uint32_t allocPc = static_cast<uint32_t>(pc);
                std::string className = resolveClassRef(cls, codeU2(code, pc + 1));
                ensureClassInitialized(classes, className, rt);
                frame.push(allocateObject(rt, classes, label, allocPc, className));
                pc += 3;
                if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                break;
            }

            case 0xbc:
            {
                uint32_t allocPc = static_cast<uint32_t>(pc);
                uint8_t atype = codeU1(code, pc + 1);
                Value countValue = frame.pop();
                std::optional<int> count = parseIntValue(countValue);
                if ((atype == 4 || atype == 5 || atype == 6 || atype == 8 || atype == 9 || atype == 10) && count.has_value() && *count >= 0) {
                    // 4=bool 5=char 6=float 8=byte 9=short 10=int — all fit in CompactArrayHeap (int32_t/element)
                    frame.push(allocatePrimitiveArray(rt, label, allocPc, static_cast<size_t>(*count)));
                } else if ((atype == 7 || atype == 11) && count.has_value() && *count >= 0) {
                    // 7=double 11=long — store as Value::ofLong in the object array heap
                    Value arr = allocateArray(rt, label, allocPc, static_cast<size_t>(*count));
                    std::optional<uint32_t> arrId = arrayId(arr);
                    if (arrId.has_value()) {
                        auto it = rt.arrays.find(*arrId);
                        if (it != rt.arrays.end()) {
                            for (Value& v : it->second) v = Value::ofLong(0);
                        }
                    }
                    frame.push(arr);
                } else {
                    frame.push(Value::named("<array>"));
                }
                pc += 2;
                break;
            }

            case 0xbd:
            {
                uint32_t allocPc = static_cast<uint32_t>(pc);
                Value countValue = frame.pop();
                std::optional<int> count = parseIntValue(countValue);
                if (count.has_value() && *count >= 0) {
                    frame.push(allocateArray(rt, label, allocPc, static_cast<size_t>(*count)));
                } else {
                    frame.push(Value::named("<array>"));
                }
                pc += 3;
                break;
            }

            case 0xc5:
            {
                uint32_t allocPc = static_cast<uint32_t>(pc);
                uint8_t dimensions = codeU1(code, pc + 3);
                std::vector<int> counts(dimensions, -1);
                for (size_t i = dimensions; i > 0; --i) {
                    std::optional<int> count = parseIntValue(frame.pop());
                    counts[i - 1] = count.has_value() ? *count : -1;
                }

                bool valid = !counts.empty();
                for (int count : counts) {
                    valid = valid && count >= 0;
                }

                if (valid) {
                    frame.push(allocateMultiArray(rt, label, allocPc, counts, 0));
                } else {
                    frame.push(Value::named("<array>"));
                }
                pc += 4;
                break;
            }

            case 0xbe:
            {
                const uint32_t t0m=statNow();
                Value arrayValue = frame.pop();
                std::optional<uint32_t> id = arrayId(arrayValue);
                if (id.has_value()) {
                    auto primIt = rt.primitiveArrays.find(*id);
                    if (primIt != rt.primitiveArrays.end()) {
                        frame.push(Value::ofInt(static_cast<int32_t>(primIt->second.size())));
                        ++pc;
                        if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                        break;
                    }
                    auto arrayIt = rt.arrays.find(*id);
                    if (arrayIt != rt.arrays.end()) {
                        frame.push(Value::ofInt(static_cast<int32_t>(arrayIt->second.size())));
                        ++pc;
                        if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                        break;
                    }
                }
                frame.push(Value::named("<arraylength:" + arrayValue.asText() + ">"));
                ++pc;
                if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                break;
            }

            case 0xbf: {
                const uint32_t throwPc = static_cast<uint32_t>(pc);
                Value exception = frame.pop();
                if (exception.isNull()) {
                    exception = allocateObject(rt, classes, label, throwPc, "java/lang/NullPointerException");
                }
                setPendingException(rt, std::move(exception), label, throwPc);
                if (catchPendingException(throwPc)) {
                    break;
                }
                runtimeFrame.pc = throwPc;
                return finish(std::nullopt);
            }

            case 0xc1: {  // instanceof
                std::string targetName = resolveClassRef(cls, codeU2(code, pc + 1));
                Value object = frame.pop();
                bool matches = false;
                std::optional<uint32_t> id = objectId(object);
                if (id.has_value()) {
                    auto objIt = rt.heap.find(*id);
                    if (objIt != rt.heap.end())
                        matches = isAssignableTo(classes, objIt->second.className, targetName);
                }
                frame.push(Value::ofInt(matches ? 1 : 0));
                pc += 3;
                break;
            }
            case 0xc2:
            case 0xc3: {  // monitorenter / monitorexit — single-threaded; pop ref and ignore
                (void)frame.pop();
                ++pc;
                break;
            }

            case 0xac:
            case 0xad:
            case 0xae:  // freturn
            case 0xaf:  // dreturn
            case 0xb0: {
                const uint32_t t0m=statNow(); Value v=frame.pop(); runtimeFrame.pc=pc; if(t0m) rt.host->miscStats.record(nowUs()-t0m); return finish(v);
            }
            case 0xb1: {
                const uint32_t t0m=statNow(); runtimeFrame.pc=pc; if(t0m) rt.host->miscStats.record(nowUs()-t0m); return finish(std::nullopt);
            }

            default: {
                const uint32_t t0m=statNow();
                pc += instructionLength(op);
                if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                break;
            }
        }
    }

    LOGF_W("resumeCurrentMethod: %s.%s%s - completed", cls.thisClass.c_str(), method.name.c_str(), method.descriptor.c_str());
    runtimeFrame.pc = pc;
    return finish(std::nullopt);
}

// Drives execution until the call stack empties, a yield is requested, or
// an uncaught exception escapes all frames. Each iteration of the loop:
//   - calls resumeCurrentMethod on the current top frame
//   - if the bytecode loop pushed a new Java frame (size grew): loop runs it
//   - if the top frame returned (size shrank): push its return value onto
//     the new top's operand stack
//   - if pendingException is set: keep looping so the next iteration's entry
//     guard can try the new top's exception table at its lastCallPc
void runTrampoline(const std::vector<ClassFile>& classes, Runtime& rt) {
    while (!rt.callStack.empty()) {
        const size_t sizeBefore = rt.callStack.size();
        std::optional<Value> returnVal = resumeCurrentMethod(classes, rt);
        if (rt.yieldRequested) return;
        if (rt.pendingException.has_value()) {
            if (rt.callStack.empty()) return;
            continue;
        }
        const size_t sizeAfter = rt.callStack.size();
        if (sizeAfter > sizeBefore) continue;        // new frame pushed; keep running
        if (returnVal.has_value() && !rt.callStack.empty()) {
            rt.callStack.back().frame.push(*returnVal);
        }
    }
}

// Variant for nested blocking sub-calls (e.g. <clinit>, hideNotify/showNotify)
// that must complete before the caller can continue. Stops once the callStack
// shrinks back to (or below) targetSize.
void runTrampolineUntilSize(
    const std::vector<ClassFile>& classes,
    Runtime& rt,
    size_t targetSize) {
    while (rt.callStack.size() > targetSize) {
        const size_t sizeBefore = rt.callStack.size();
        std::optional<Value> returnVal = resumeCurrentMethod(classes, rt);
        if (rt.yieldRequested) return;
        if (rt.pendingException.has_value()) {
            if (rt.callStack.size() <= targetSize) return;
            continue;
        }
        const size_t sizeAfter = rt.callStack.size();
        if (sizeAfter > sizeBefore) continue;
        if (returnVal.has_value() && !rt.callStack.empty()) {
            rt.callStack.back().frame.push(*returnVal);
        }
    }
}

void resetRuntimeTrace(Runtime& rt) {
    bool recording = rt.trace.recording;
    rt.trace = ExecutionTrace{};
    rt.trace.recording = recording;
    rt.taskMethodProfiles.clear();
    rt.taskNativeProfiles.clear();
    clearPendingException(rt);
    rt.pendingYieldRethrowStartUs = 0;
    rt.yieldRequested = false;
    rt.yieldMillis = 0;
    rt.steps = 0;
    rt.callStack.clear();
    restoreCallStackReserve(rt);
    rt.graphicsFramebuffer = nullptr;
    rt.graphicsWidth = 0;
    rt.graphicsHeight = 0;
    rt.graphicsColorRgb = 0x000000;
    rt.mainFbCanvas.reset();
}

class ScopedResourceReadTrace final {
public:
    explicit ScopedResourceReadTrace(ExecutionTrace& trace) {
        port::setResourceReadObserver([&trace](const port::ResourceReadEvent& event) {
            trace.resourceReads.push_back(ResourceRead{
                event.path,
                event.resolvedPath,
                event.ok,
                event.bytes,
                event.durationMillis,
            });
        });
    }

    ~ScopedResourceReadTrace() {
        port::setResourceReadObserver({});
    }
};

ExecutionTrace startSession(MidletSession& session) {
    Runtime& rt = session.runtime();
    resetRuntimeTrace(rt);
    ScopedResourceReadTrace resourceReadTrace(rt.trace);

    const std::vector<ClassFile>& classes = session.classes();
    const ClassFile* midletClass = findClass(classes, session.className());
    if (midletClass == nullptr) {
        MethodRef missing{session.className(), "<load>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, missing, {});
        return rt.trace;
    }

    session.midletRef() = allocateObject(rt, classes, "<midlet>", 0, midletClass->thisClass);
    const MethodInfo* init = findDeclaredMethod(*midletClass, "<init>", "()V");
    if (init != nullptr) {
        std::vector<Value> initArgs = {session.midletRef()};
        if (pushJavaFrame(rt, classes, *midletClass, *init, initArgs)) {
            runTrampoline(classes, rt);
        }
        if (rt.pendingException.has_value()) {
            recordUncaughtException(rt, "<midlet-init>");
            clearPendingException(rt);
            return rt.trace;
        }
    } else {
        MethodRef ref{midletClass->thisClass, "<init>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {session.midletRef()});
    }

    const ClassFile* startOwner = nullptr;
    const MethodInfo* startApp = findMethodInHierarchy(classes, *midletClass, "startApp", "()V", &startOwner);
    if (startOwner != nullptr && startApp != nullptr) {
        std::vector<Value> startArgs = {session.midletRef()};
        rt.stepLimitYieldEnabled = true;
        if (pushJavaFrame(rt, classes, *startOwner, *startApp, startArgs)) {
            runTrampoline(classes, rt);
            // If the step limit fired mid-startApp, callStack still has frames.
            // Cap at 10 budgets (~1M steps) so a truly-infinite startApp doesn't hang.
            for (int budget = 0; !rt.callStack.empty() && !rt.pendingException.has_value() && budget < 10; ++budget) {
                rt.yieldRequested = false;
                rt.steps = 0;
                runTrampoline(classes, rt);
            }
        }
        rt.stepLimitYieldEnabled = false;
        rt.yieldRequested = false;
        rt.callStack.clear();
        if (rt.pendingException.has_value()) {
            recordUncaughtException(rt, "<midlet-start>");
            clearPendingException(rt);
        }
    } else {
        MethodRef ref{midletClass->thisClass, "startApp", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {session.midletRef()});
    }

    session.setStarted(true);
    return rt.trace;
}

void dispatchCanvasKeyEvent(MidletSession& session, const HostKeyEvent& event) {
    // std::cout << "dispatchCanvasKeyEvent: " << (event.type == HostKeyEventType::Press ? "Press" : "Release") << " code=" << event.keyCode << std::endl;
    Runtime& rt = session.runtime();

    std::optional<uint32_t> displayableId = objectId(rt.currentDisplayable);
    if (!displayableId.has_value()) {
        return;
    }
    auto displayableIt = rt.heap.find(*displayableId);
    if (displayableIt == rt.heap.end()) {
        return;
    }

    const std::vector<ClassFile>& classes = session.classes();
    if (!isClassOrSubclassOf(classes, displayableIt->second.className, "javax/microedition/lcdui/Canvas")) {
        return;
    }

    const char* methodName = event.type == HostKeyEventType::Press ? "keyPressed" : "keyReleased";
    const ClassFile* owner = nullptr;
    const MethodInfo* handler = findMethodInHierarchy(
        classes,
        displayableIt->second.className,
        methodName,
        "(I)V",
        &owner);
    if (owner == nullptr || handler == nullptr) {
        return;
    }

    std::vector<Value> keyArgs = {rt.currentDisplayable, Value::named(std::to_string(event.keyCode))};
    if (pushJavaFrame(rt, classes, *owner, *handler, keyArgs)) {
        runTrampoline(classes, rt);
    }
    if (rt.pendingException.has_value()) {
        recordUncaughtException(rt, "<input>");
        clearPendingException(rt);
    }
    // std::cout << "dispatchCanvasKeyEvent: executed " << displayableIt->second.className << "::" << methodName << std::endl;
    // {
    //     // Debug: print key-gating fields from the displayable object
    //     HeapObject& dbgObj = displayableIt->second;
    //     if (dbgObj.cls != nullptr) {
    //         buildFieldSlots(rt, classes, *dbgObj.cls);
    //         auto& slotMap = rt.fieldSlotCache[dbgObj.cls];
    //         auto printField = [&](const char* name) {
    //             auto it = slotMap.find(name);
    //             if (it != slotMap.end() && it->second < dbgObj.fields.size()) {
    //                 std::cout << "  field " << name << "=" << dbgObj.fields[it->second].asText() << "\n";
    //             } else {
    //                 std::cout << "  field " << name << "=<missing>\n";
    //             }
    //         };
    //         printField("ap");
    //         printField("I");
    //         printField("au");
    //     }
    // }
    rt.repaintRequested = true;
}

ExecutionTrace renderSession(MidletSession& session, uint16_t* pixels, int width, int height) {
    Runtime& rt = session.runtime();
    const bool profileFrame = rt.host != nullptr && rt.host->profileFrameTimings;
    const uint32_t profileStartUs = profileFrame ? nowUs() : 0;
    resetRuntimeTrace(rt);
    const bool captureTraceDetails = rt.trace.recording || profileFrame;
    ScopedResourceReadTrace resourceReadTrace(rt.trace);
    rt.graphicsFramebuffer = pixels;
    rt.graphicsWidth = width;
    rt.graphicsHeight = height;
    rt.mainFbCanvas.emplace(width, height, pixels);
    auto finishProfile = [&]() -> ExecutionTrace {
        if (profileFrame && rt.host != nullptr) {
            rt.trace.frameProfile.renderSessionUs = nowUs() - profileStartUs;
            rt.trace.frameProfile.steps = static_cast<uint32_t>(rt.steps);
            captureTaskMethodProfiles(rt);
            captureTaskNativeProfiles(rt);
            rt.host->recordFrameProfile(
                rt.trace.frameProfile,
                rt.trace.taskMethodProfiles,
                rt.trace.taskNativeProfiles);
        }
        return rt.trace;
    };
    auto captureSuspendedForTrace = [&]() {
        if (!captureTraceDetails) {
            return;
        }
        const uint32_t suspendedTraceStartUs = profileFrame ? nowUs() : 0;
        captureSuspendedTasks(rt.trace, session);
        if (profileFrame) {
            rt.trace.frameProfile.suspendedTraceUs += nowUs() - suspendedTraceStartUs;
        }
    };

    if (!session.started()) {
        return startSession(session);
    }

    const uint32_t inputStartUs = profileFrame ? nowUs() : 0;
    if (rt.host != nullptr) {
        std::vector<HostKeyEvent> inputEvents = rt.host->drainInputEvents();
        if (profileFrame) {
            rt.trace.frameProfile.inputEvents = static_cast<uint16_t>(
                inputEvents.size() > 0xffffu ? 0xffffu : inputEvents.size());
        }
        for (const HostKeyEvent& event : inputEvents) {
            dispatchCanvasKeyEvent(session, event);
        }
    }
    if (profileFrame) {
        rt.trace.frameProfile.inputUs = nowUs() - inputStartUs;
    }

    const uint32_t tasksStartUs = profileFrame ? nowUs() : 0;
    const uint32_t now = rt.host != nullptr ? rt.host->millis() : 0;
    auto resumeTaskStack = [&]() {
        runTrampoline(session.classes(), rt);
    };
    for (ThreadTask& task : session.tasks()) {
        if (task.finished) {
            continue;
        }
        if (now < task.wakeAtMillis) {
            if (profileFrame) {
                ++rt.trace.frameProfile.taskSkippedSleeping;
            }
            continue;
        }

        if (profileFrame) {
            ++rt.trace.frameProfile.taskRuns;
        }
        rt.currentTask = &task;
        rt.pendingException = task.pendingException;
        task.pendingException.reset();
        std::string taskLabel = task.method != nullptr
            ? task.cls->thisClass + "." + task.method->name + task.method->descriptor
            : std::string("<task>");
        const uint32_t taskInvokeStartUs = profileFrame ? nowUs() : 0;
        bool taskYielded = false;
        const bool previousStepLimitYieldEnabled = rt.stepLimitYieldEnabled;
        rt.stepLimitYieldEnabled = true;
        try {
            if (!task.suspendedFrames.empty()) {
                resumeCallStackFrom(rt, task);
                resumeTaskStack();
            } else if (task.cls != nullptr && task.method != nullptr) {
                std::vector<Value> taskArgs = {task.receiver};
                if (pushJavaFrame(rt, session.classes(), *task.cls, *task.method, taskArgs)) {
                    runTrampoline(session.classes(), rt);
                }
            }
            if (rt.pendingException.has_value()) {
                LOGF_W("[task-died] %s uncaught=%s thrownAt=%s pc=%u", taskLabel.c_str(),
                    exceptionClassName(rt, *rt.pendingException).c_str(),
                    rt.pendingExceptionMethodLabel.c_str(),
                    rt.pendingExceptionPc);
                recordUncaughtException(rt, taskLabel);
                recordThreadDeath(rt, taskLabel);
                task.finished = true;
                clearPendingException(rt);
                rt.yieldRequested = false;
                rt.yieldMillis = 0;
            } else if (rt.yieldRequested) {
                const uint32_t yieldStartUs = profileFrame ? nowUs() : 0;
                if (profileFrame) {
                    rt.trace.frameProfile.taskInvokeUs += yieldStartUs - taskInvokeStartUs;
                    ++rt.trace.frameProfile.taskSleepYields;
                    rt.trace.frameProfile.taskSleepRequestedMs += rt.yieldMillis;
                }
                taskYielded = true;
                task.wakeAtMillis = now + rt.yieldMillis;
                if (!rt.callStack.empty()) {
                    const uint32_t suspendSaveStartUs = profileFrame ? nowUs() : 0;
                    suspendCallStackInto(rt, task);
                    if (profileFrame) {
                        rt.trace.frameProfile.taskSuspendSaveUs += nowUs() - suspendSaveStartUs;
                    }
                }
                rt.yieldRequested = false;
                rt.yieldMillis = 0;
                if (profileFrame) {
                    rt.trace.frameProfile.taskCatchUs += nowUs() - yieldStartUs;
                }
            } else {
                LOGF_W("[task-done] %s returned normally", taskLabel.c_str());
                task.finished = true;
            }
        } catch (const YieldThreadSleep& request) {
            const uint32_t catchStartUs = profileFrame ? nowUs() : 0;
            if (profileFrame) {
                rt.trace.frameProfile.taskInvokeUs += catchStartUs - taskInvokeStartUs;
                ++rt.trace.frameProfile.taskSleepYields;
                rt.trace.frameProfile.taskSleepRequestedMs += request.millis;
            }
            taskYielded = true;
            task.wakeAtMillis = now + request.millis;
            if (!rt.callStack.empty()) {
                const uint32_t suspendSaveStartUs = profileFrame ? nowUs() : 0;
                suspendCallStackInto(rt, task);
                if (profileFrame) {
                    rt.trace.frameProfile.taskSuspendSaveUs += nowUs() - suspendSaveStartUs;
                }
            }
            if (profileFrame) {
                rt.trace.frameProfile.taskCatchUs += nowUs() - catchStartUs;
            }
        }
        rt.stepLimitYieldEnabled = previousStepLimitYieldEnabled;
        if (profileFrame && !taskYielded) {
            rt.trace.frameProfile.taskInvokeUs += nowUs() - taskInvokeStartUs;
        }
        if (taskYielded) {
            task.pendingException = rt.pendingException;
        }
        clearPendingException(rt);
        rt.currentTask = nullptr;
        const uint32_t clearStartUs = profileFrame ? nowUs() : 0;
        rt.callStack.clear();
        if (profileFrame) {
            rt.trace.frameProfile.taskClearUs += nowUs() - clearStartUs;
        }
    }
    if (profileFrame) {
        rt.trace.frameProfile.tasksUs = nowUs() - tasksStartUs;
    }

    // {
        // std::optional<uint32_t> dbgId = objectId(rt.currentDisplayable);
        // if (dbgId.has_value()) {
        //     auto dbgIt = rt.heap.find(*dbgId);
        //     if (dbgIt != rt.heap.end()) {
        //         HeapObject& dbgObj = dbgIt->second;
        //         if (dbgObj.cls != nullptr) {
        //             buildFieldSlots(rt, session.classes(), *dbgObj.cls);
        //             auto& slotMap = rt.fieldSlotCache[dbgObj.cls];
        //             auto dumpField = [&](const char* name) {
        //                 auto it = slotMap.find(name);
        //                 if (it != slotMap.end() && it->second < dbgObj.fields.size() && dbgObj.fields[it->second].isInitialized()) {
        //                     std::cout << "  post-task " << name << "=" << dbgObj.fields[it->second].asText() << "\n";
        //                 }
        //             };
        //             auto dumpStatic = [&](const char* key) {
        //                 auto it = rt.staticFields.find(key);
        //                 if (it != rt.staticFields.end()) {
        //                     std::cout << "  static " << key << "=" << it->second.asText() << "\n";
        //                 }
        //             };
        //             auto dumpArrayLen = [&](const char* name) {
        //                 auto it = slotMap.find(name);
        //                 if (it == slotMap.end() || it->second >= dbgObj.fields.size()) return;
        //                 const Value& v = dbgObj.fields[it->second];
        //                 std::optional<uint32_t> arrId = arrayId(v);
        //                 if (!arrId.has_value()) { std::cout << "  post-task " << name << "=null\n"; return; }
        //                 auto primIt = rt.primitiveArrays.find(*arrId);
        //                 if (primIt != rt.primitiveArrays.end()) { std::cout << "  post-task " << name << ".len=" << primIt->second.size() << "\n"; return; }
        //                 auto arrIt = rt.arrays.find(*arrId);
        //                 if (arrIt != rt.arrays.end()) { std::cout << "  post-task " << name << ".len=" << arrIt->second.size() << "\n"; }
        //             };
        //             // dumpStatic("MainCanvas.aq|I");
        //             // dumpStatic("MainCanvas.ar|I");
        //             // dumpStatic("MainCanvas.aE|I");
        //             // dumpStatic("MainCanvas.aF|I");
        //             // dumpField("ap");
        //             // dumpField("as");
        //             // dumpField("at");
        //             // dumpArrayLen("h"); // tile data byte[] - null means aY() failed
        //         }
        //     }
        // }
    // }

    const uint32_t displayLookupStartUs = profileFrame ? nowUs() : 0;
    std::optional<uint32_t> displayableId = objectId(rt.currentDisplayable);
    if (!displayableId.has_value()) {
        if (profileFrame) {
            rt.trace.frameProfile.displayLookupUs = nowUs() - displayLookupStartUs;
        }
        captureSuspendedForTrace();
        return finishProfile();
    }
    auto displayableIt = rt.heap.find(*displayableId);
    if (displayableIt == rt.heap.end()) {
        if (profileFrame) {
            rt.trace.frameProfile.displayLookupUs = nowUs() - displayLookupStartUs;
        }
        captureSuspendedForTrace();
        return finishProfile();
    }
    if (profileFrame) {
        rt.trace.frameProfile.displayableFound = true;
    }
    rt.trace.currentDisplayableClass = displayableIt->second.className;
    if (profileFrame) {
        rt.trace.frameProfile.displayLookupUs = nowUs() - displayLookupStartUs;
    }

    const std::vector<ClassFile>& classes = session.classes();
    const uint32_t paintLookupStartUs = profileFrame ? nowUs() : 0;
    const ClassFile* paintOwner = nullptr;
    const MethodInfo* paint = findMethodInHierarchy(
        classes,
        displayableIt->second.className,
        "paint",
        "(Ljavax/microedition/lcdui/Graphics;)V",
        &paintOwner);
    if (profileFrame) {
        rt.trace.frameProfile.paintLookupUs = nowUs() - paintLookupStartUs;
        rt.trace.frameProfile.repaintRequested = rt.repaintRequested;
    }
    // Only paint when the app has explicitly requested a repaint. Sleeping tasks
    // do not force paint — the app is responsible for calling repaint() when
    // visual state changes. Forcing paint every frame on any live task causes
    // frame-counting animations (where paint increments a counter) to advance
    // orders of magnitude faster than the game's timer expects.
    if (!rt.repaintRequested) {
        captureSuspendedForTrace();
        return finishProfile();
    }

    const uint32_t paintStartUs = profileFrame ? nowUs() : 0;
    if (paintOwner != nullptr && paint != nullptr) {
        if (profileFrame) {
            rt.trace.frameProfile.paintCalled = true;
        }
        // J2ME spec: Graphics object starts each paint() with translate=(0,0) and full clip.
        if (rt.mainFbCanvas.has_value()) {
            rt.mainFbCanvas.emplace(rt.graphicsWidth, rt.graphicsHeight, rt.graphicsFramebuffer);
        }
        std::vector<Value> paintArgs = {rt.currentDisplayable, Value::ofInt(Value::kHandleGfxTag | 0)};
        if (pushJavaFrame(rt, classes, *paintOwner, *paint, paintArgs)) {
            runTrampoline(classes, rt);
        }
        if (rt.pendingException.has_value()) {
            recordUncaughtException(rt, "<paint>");
            clearPendingException(rt);
        }
        const bool isGameCanvas = isClassOrSubclassOf(
            classes, displayableIt->second.className,
            "javax/microedition/lcdui/game/GameCanvas");
        rt.trace.framePresented = !isGameCanvas || rt.gameCanvasFlushCommitted;
        rt.gameCanvasFlushCommitted = false;
    } else {
        MethodRef ref{displayableIt->second.className, "paint", "(Ljavax/microedition/lcdui/Graphics;)V"};
        (void)recordUnknownCall(rt, "<render>", 0, ref, {rt.currentDisplayable, Value::ofInt(Value::kHandleGfxTag | 0)});
    }
    if (profileFrame) {
        rt.trace.frameProfile.paintUs = nowUs() - paintStartUs;
    }
    rt.repaintRequested = false;

    captureSuspendedForTrace();

    return finishProfile();
}

} // namespace

std::shared_ptr<MidletSession> createMidletSession(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host) {
    return std::make_shared<MidletSession>(classes, className, host);
}

ExecutionTrace startMidletSession(MidletSession& session) {
    return startSession(session);
}

void setTraceRecording(MidletSession& session, bool enabled) {
    session.runtime().trace.recording = enabled;
}

ExecutionTrace renderMidletSession(
    MidletSession& session,
    uint16_t* pixels,
    int width,
    int height) {
    return renderSession(session, pixels, width, height);
}

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method) {
    Runtime rt;
    rt.callStack.reserve(kMaxCallDepth + 1);
    std::vector<Value> emptyArgs;
    if (pushJavaFrame(rt, classes, cls, method, emptyArgs)) {
        runTrampoline(classes, rt);
    }
    if (rt.pendingException.has_value()) {
        recordUncaughtException(rt, "<main>");
        clearPendingException(rt);
    }
    return rt.trace;
}

ExecutionTrace executeMidlet(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host) {
    std::shared_ptr<MidletSession> session = createMidletSession(classes, className, host);
    return startMidletSession(*session);
}

ExecutionTrace renderMidletFrame(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host,
    uint16_t* pixels,
    int width,
    int height) {
    std::shared_ptr<MidletSession> session = createMidletSession(classes, className, host);
    (void)startMidletSession(*session);
    return renderMidletSession(*session, pixels, width, height);
}

} // namespace jvmpoc
