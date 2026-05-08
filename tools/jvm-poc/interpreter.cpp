#include "interpreter.hpp"

#include "bytecode.hpp"
#include "jvm_host.hpp"
#include "method_execution_delegate.hpp"
#include "method_resolution.hpp"
#include "native_methods.hpp"
#include "sram_allocator.hpp"
#include "j2me_port/J2MECompat.hpp"

#ifdef ESP32_BUILD
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace jvmpoc {
namespace {

constexpr size_t kMaxSteps = 100000;
constexpr size_t kMaxCallDepth = 64;
constexpr uint16_t kAccNative = 0x0100;
constexpr const char* kClassHandlePrefix = "class:";
constexpr const char* kResourceStreamHandlePrefix = "resource-stream:";

uint32_t branchTarget(size_t pc, int16_t offset) {
    return static_cast<uint32_t>(static_cast<int32_t>(pc) + offset);
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
};

struct HeapObject {
    std::string className;
    const ClassFile* cls = nullptr; // cached class pointer for fast virtual dispatch (item K)
    std::vector<Value> fields;      // indexed by slot from fieldSlotCache (item D)
};

using Heap = std::unordered_map<uint32_t, HeapObject>;
using ArrayHeap = std::unordered_map<uint32_t, std::vector<Value>>;
using StringHeap = std::unordered_map<uint32_t, std::string>;
using ImageHeap = std::unordered_map<uint32_t, port::Image>;
using ResourceImageCache = std::map<std::string, uint32_t>;

struct RuntimeFrame {
    std::string label;
    const ClassFile* cls = nullptr;
    const MethodInfo* method = nullptr;
    size_t pc = 0;
    Frame frame;
};

struct ThreadTask {
    const ClassFile* cls = nullptr;
    const MethodInfo* method = nullptr;
    Value receiver = Value::named("0");
    std::optional<RuntimeFrame> suspendedFrame;
    uint32_t wakeAtMillis = 0;
    bool finished = false;
};

struct Runtime {
    const JvmHost* host = nullptr;
    MidletSession* session = nullptr;
    ThreadTask* currentTask = nullptr;
    std::unordered_map<std::string, Value> staticFields;
    Heap heap;
    ArrayHeap arrays;
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
    // Build-time maps: cls* → {fieldName → slot} and cls* → total slot count.
    std::unordered_map<const ClassFile*, std::unordered_map<std::string, uint16_t>> fieldSlotCache;
    std::unordered_map<const ClassFile*, uint16_t> fieldSlotCount;
    Value displayRef = Value::named("display#1");
    Value currentDisplayable = Value::named("0");
    uint16_t* graphicsFramebuffer = nullptr;
    int graphicsWidth = 0;
    int graphicsHeight = 0;
    int graphicsColorRgb = 0x000000;
    ExecutionTrace trace;
    size_t steps = 0;
    bool repaintRequested = true;
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
        if (mySlots.count(f.name) == 0) mySlots[f.name] = nextSlot++;
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
        if (task.suspendedFrame.has_value()) {
            state += " suspended=" + task.suspendedFrame->label + " pc=" + std::to_string(task.suspendedFrame->pc);
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

std::optional<uint32_t> objectId(const Value& value);
std::optional<uint32_t> arrayId(const Value& value);

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
    Value loaded = Value::ofInt(0);
    std::optional<uint32_t> id = arrayId(arrayValue);
    std::optional<int> index = parseIntValue(indexValue);
    if (id.has_value() && index.has_value()) {
        auto arrayIt = rt.arrays.find(*id);
        if (arrayIt != rt.arrays.end() && *index >= 0 &&
            static_cast<size_t>(*index) < arrayIt->second.size()) {
            loaded = arrayIt->second[static_cast<size_t>(*index)];
        }
    }
    return loaded;
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
    if (id.has_value() && index.has_value()) {
        auto arrayIt = rt.arrays.find(*id);
        if (arrayIt != rt.arrays.end() && *index >= 0 &&
            static_cast<size_t>(*index) < arrayIt->second.size()) {
            arrayIt->second[static_cast<size_t>(*index)] = value;
        }
    }
    if (rt.trace.recording) rt.trace.arrayWrites.push_back(ArrayWrite{label, pc, arrayValue, indexValue, value});
}

NativeCallResult handleBuiltInInstanceCall(Runtime& rt, const MethodRef& ref, const std::vector<Value>& args) {
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
        return NativeCallResult{true, path.empty()
            ? std::optional<Value>(Value::named("0"))
            : std::optional<Value>(Value::named(std::string(kResourceStreamHandlePrefix) + path))};
    }

    if (ref.className == "java/io/InputStream" && ref.name == "read" && ref.descriptor == "([B)I") {
        if (args.size() < 2) {
            return NativeCallResult{true, Value::named("0")};
        }

        std::optional<std::string> path = parseTextHandle(args[0], kResourceStreamHandlePrefix);
        std::optional<uint32_t> id = arrayId(args[1]);
        auto arrayIt = id.has_value() ? rt.arrays.find(*id) : rt.arrays.end();
        if (!path.has_value() || !id.has_value() || arrayIt == rt.arrays.end()) {
            return NativeCallResult{true, Value::named("0")};
        }

        std::vector<uint8_t> data;
        if (!port::readResourceAll(*path, data) || data.empty()) {
            return NativeCallResult{true, Value::named("0")};
        }

        const int count = std::min(static_cast<int>(arrayIt->second.size()), static_cast<int>(data.size()));
        for (int i = 0; i < count; ++i) {
            arrayIt->second[static_cast<size_t>(i)] =
                Value::named(std::to_string(static_cast<int>(static_cast<int8_t>(data[static_cast<size_t>(i)]))));
        }
        return NativeCallResult{true, Value::named(std::to_string(count))};
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
    rt.trace.unknownMethodCalls.push_back(UnknownMethodCall{
        label,
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

void captureDisplayableFields(ExecutionTrace& trace, const Runtime& rt, const HeapObject& object) {
    static const char* kInterestingFields[] = {
        "screen",
        "state",
        "ani_step",
        "p_mode",
        "m_mode",
        "game_on",
        "t_game_on",
        "msg",
        "h_x",
        "h_y",
        "h_dir",
        "auto_move",
        "w_dir",
        "stage",
        "chap",
    };

    trace.currentDisplayableFields.clear();
    for (const char* fieldName : kInterestingFields) {
        auto slotCacheIt = rt.fieldSlotCache.find(object.cls);
        if (slotCacheIt == rt.fieldSlotCache.end()) continue;
        auto nameIt = slotCacheIt->second.find(fieldName);
        if (nameIt == slotCacheIt->second.end()) continue;
        uint16_t slot = nameIt->second;
        if (slot >= object.fields.size() || !object.fields[slot].isInitialized()) continue;
        trace.currentDisplayableFields.push_back(
            std::string(fieldName) + "=" + debugValueText(rt, object.fields[slot]));
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
    for (const RuntimeFrame& runtimeFrame : rt.callStack) {
        const Frame::ValueVec& locals = runtimeFrame.frame.locals();
        for (size_t i = 0; i < locals.size(); ++i) {
            addRoot(report, runtimeFrame.label + " local[" + std::to_string(i) + "]", locals[i], rt, markedObjects, markedArrays);
        }

        const Frame::ValueVec& stack = runtimeFrame.frame.stack();
        for (size_t i = 0; i < stack.size(); ++i) {
            addRoot(report, runtimeFrame.label + " stack[" + std::to_string(i) + "]", stack[i], rt, markedObjects, markedArrays);
        }
    }
    if (rt.session != nullptr) {
        for (const ThreadTask& task : rt.session->tasks()) {
            if (!task.suspendedFrame.has_value()) {
                continue;
            }
            const Frame::ValueVec& locals = task.suspendedFrame->frame.locals();
            for (size_t i = 0; i < locals.size(); ++i) {
                addRoot(report, task.suspendedFrame->label + " local[" + std::to_string(i) + "]", locals[i], rt, markedObjects, markedArrays);
            }
            const Frame::ValueVec& stack = task.suspendedFrame->frame.stack();
            for (size_t i = 0; i < stack.size(); ++i) {
                addRoot(report, task.suspendedFrame->label + " stack[" + std::to_string(i) + "]", stack[i], rt, markedObjects, markedArrays);
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
        rt.arrays.erase(id);
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

std::optional<Value> executeMethod(
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    std::vector<Value>& args,
    Runtime& rt,
    size_t depth);

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
        rt.session->tasks().push_back(ThreadTask{owner, run, runnable, std::nullopt, 0, false});
        return;
    }
    std::vector<Value> runArgs = {runnable};
    (void)executeMethod(classes, *owner, *run, runArgs, rt, 0);
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

std::optional<Value> resumeCurrentMethod(
    const std::vector<ClassFile>& classes,
    Runtime& rt,
    size_t depth) {
    if (depth > kMaxCallDepth || rt.callStack.empty()) {
        return Value::named("<call-depth-limit>");
    }

    RuntimeFrame& runtimeFrame = rt.callStack.back();
    const ClassFile& cls = *runtimeFrame.cls;
    const MethodInfo& method = *runtimeFrame.method;
    const std::string& label = runtimeFrame.label;
    Frame& frame = runtimeFrame.frame;
    size_t pc = runtimeFrame.pc;

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
                    throw YieldThreadSleep{millis};
                }
                if (rt.host != nullptr) {
                    rt.host->sleepMillis(millis);
                }
            },
            [&]() {
                rt.repaintRequested = true;
            },
            rt.strings,
            rt.arrays,
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

    // When no host is attached (or host has no stats to collect) skip all
    // per-bytecode nowUs() calls: each costs ~6µs and there are ~13 000/frame.
    const bool kCollectStats = (rt.host != nullptr && rt.host->collectBytecodeStats);
    // Zero-cost wrapper: returns nowUs() only when stats are active, 0 otherwise.
    // "if (kCollectStats)" is a compile-time-predictable branch; the nowUs() call
    // is completely absent from the hot path when kCollectStats==false.
    auto statNow = [&]() -> uint32_t {
        return kCollectStats ? nowUs() : 0u;
    };

    while (pc < codeSize) {
        if (++rt.steps > kMaxSteps) {
            rt.trace.stepLimitHit = true;
            runtimeFrame.pc = pc;
            captureStackSnapshot(rt.trace, rt.callStack);
            return finish(std::nullopt);
        }
        if (rt.host != nullptr) ++rt.host->bytecodeSteps;

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
            case 0x2a: { const uint32_t t0 = statNow(); frame.push(frame.local(0)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2b: { const uint32_t t0 = statNow(); frame.push(frame.local(1)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2c: { const uint32_t t0 = statNow(); frame.push(frame.local(2)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }
            case 0x2d: { const uint32_t t0 = statNow(); frame.push(frame.local(3)); ++pc; if(t0) rt.host->localLoadStats.record(nowUs()-t0); break; }

            case 0x2e:
            case 0x32:
            case 0x33:
            case 0x34: {
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
            case 0x4b: { const uint32_t t0=statNow(); store(0, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4c: { const uint32_t t0=statNow(); store(1, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4d: { const uint32_t t0=statNow(); store(2, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }
            case 0x4e: { const uint32_t t0=statNow(); store(3, static_cast<uint32_t>(pc)); ++pc; if(t0) rt.host->storeStats.record(nowUs()-t0); break; }

            case 0x4f:
            case 0x53:
            case 0x54:
            case 0x55: {
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

            case 0x92:
                ++pc;
                break;

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

            case 0xa7: {
                const uint32_t t0b=statNow();
                pc = branchTarget(pc, codeS2(code, pc + 1));
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
                        return rt.staticKeyCache.emplace(skey, ref.className + "." + ref.name).first->second;
                    }();
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
                        return rt.staticKeyCache.emplace(skey, ref.className + "." + ref.name).first->second;
                    }();
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
                        const uint64_t fidxKey = callCacheKey(obj.cls, cpIdx);
                        auto fidxIt = rt.fieldIndexCache.find(fidxKey);
                        if (fidxIt != rt.fieldIndexCache.end()) {
                            // Hot path: integer-keyed SRAM lookup → direct vector index.
                            const uint16_t slot = fidxIt->second;
                            if (slot < obj.fields.size()) {
                                const Value& sv = obj.fields[slot];
                                if (sv.isInitialized()) value = sv;
                            }
                        } else {
                            // Cold path: resolve field name, find slot, populate cache.
                            const std::string& fieldName = resolveFieldName(rt, cls, cpIdx);
                            if (obj.cls != nullptr) {
                                buildFieldSlots(rt, classes, *obj.cls);
                                const auto& slotMap = rt.fieldSlotCache[obj.cls];
                                auto nameIt = slotMap.find(fieldName);
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
                    const uint64_t fidxKey = callCacheKey(obj.cls, cpIdx);
                    auto fidxIt = rt.fieldIndexCache.find(fidxKey);
                    if (fidxIt != rt.fieldIndexCache.end()) {
                        // Hot path: integer-keyed SRAM lookup → direct vector index.
                        const uint16_t slot = fidxIt->second;
                        if (slot >= obj.fields.size()) obj.fields.resize(slot + 1);
                        obj.fields[slot] = value;
                    } else {
                        // Cold path: resolve field name, find slot, populate cache.
                        const std::string& fieldName = resolveFieldName(rt, cls, cpIdx);
                        if (obj.cls != nullptr) {
                            buildFieldSlots(rt, classes, *obj.cls);
                            const auto& slotMap = rt.fieldSlotCache[obj.cls];
                            auto nameIt = slotMap.find(fieldName);
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
                const uint64_t ckey = callCacheKey(&cls, cpIdx);

                const ClassFile* targetClass = nullptr;
                const MethodInfo* targetMethod = nullptr;
                size_t argSlots = 0;
                bool isNativeCall = false;
                MethodRef ref;
                bool haveRef = false;

                auto cit = rt.callCache.find(ckey);
                if (cit != rt.callCache.end()) {
                    argSlots = cit->second.argSlots;
                    targetClass = cit->second.targetClass;
                    targetMethod = cit->second.method;
                    isNativeCall = cit->second.isNative;
                } else {
                    ref = resolveMethodRef(cls, cpIdx);
                    haveRef = true;
                    argSlots = argumentSlotWidths(ref.descriptor).size();
                    targetMethod = findMethodInHierarchy(
                        classes, ref.className, ref.name, ref.descriptor, &targetClass);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        isNativeCall = hasAccess(targetMethod->access, kAccNative);
                        rt.callCache[ckey] = {static_cast<uint8_t>(argSlots), isNativeCall, "", nullptr, targetClass, targetMethod};
                    }
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
                        const uint32_t tN = (rt.host && rt.host->profileNatives) ? nowUs() : 0;
                        try {
                            nativeResult = handleNativeStaticCall(
                                sharedNativeCtx, label, callPc, nativeRef, rt.callArgsBuf);
                        } catch (const YieldThreadSleep&) {
                            pc += 3;
                            runtimeFrame.pc = pc;
                            throw;
                        }
                        if (tN) rt.host->nativeStats[nativeRef.className + "." + nativeRef.name].record(nowUs() - tN);
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
                        runtimeFrame.pc = pc;
                        const uint32_t tDisp0 = nowUs() - t0inv;
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, rt.callArgsBuf, rt, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                        pc += 3;
                        if(t0inv) rt.host->invokeStats.record(tDisp0);
                        break;
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
                        lookupClassName = cit->second.runtimeClass;
                        lookupClassFromCache = true;
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
                if (cacheClassMatch) {
                    targetClass = cit->second.targetClass;
                    targetMethod = cit->second.method;
                    isNativeCall = cit->second.isNative;
                } else {
                    const std::string& lookupName = haveRef ? ref.name : cit->second.method->name;
                    const std::string& lookupDesc = haveRef ? ref.descriptor : cit->second.method->descriptor;
                    targetMethod = findMethodInHierarchy(
                        classes, lookupClassName, lookupName, lookupDesc, &targetClass);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        isNativeCall = hasAccess(targetMethod->access, kAccNative);
                        rt.callCache[ckey] = {static_cast<uint8_t>(argSlots), isNativeCall,
                                              lookupClassName, findClass(classes, lookupClassName),
                                              targetClass, targetMethod};
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
                        const uint32_t tN = (rt.host && rt.host->profileNatives) ? nowUs() : 0;
                        sharedNativeCtx.tProfT0 = tN;
                        sharedNativeCtx.tProfTEntry = 0;
                        try {
                            nativeResult = handleNativeInstanceCall(
                                sharedNativeCtx, label, static_cast<uint32_t>(pc), nativeRef, rt.callArgsBuf);
                        } catch (const YieldThreadSleep&) {
                            pc += op == 0xb9 ? 5 : 3;
                            runtimeFrame.pc = pc;
                            throw;
                        }
                        if (tN) rt.host->nativeStats[nativeRef.className + "." + nativeRef.name].record(nowUs() - tN);
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
                        runtimeFrame.pc = pc;
                        const uint32_t tDisp0 = nowUs() - t0inv;
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, rt.callArgsBuf, rt, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                        pc += op == 0xb9 ? 5 : 3;
                        if(t0inv) rt.host->invokeStats.record(tDisp0);
                        break;
                    }
                } else {
                    if (!haveRef) ref = resolveMethodRef(cls, cpIdx);
                    NativeCallResult builtInResult = handleBuiltInInstanceCall(rt, ref, rt.callArgsBuf);
                    if (builtInResult.handled) {
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
                if ((atype == 4 || atype == 5 || atype == 8 || atype == 10) && count.has_value() && *count >= 0) {
                    frame.push(allocateArray(rt, label, allocPc, static_cast<size_t>(*count)));
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
                auto arrayIt = id.has_value() ? rt.arrays.find(*id) : rt.arrays.end();
                if (id.has_value() && arrayIt != rt.arrays.end()) {
                    frame.push(Value::ofInt(static_cast<int32_t>(arrayIt->second.size())));
                } else {
                    frame.push(Value::named("<arraylength:" + arrayValue.asText() + ">"));
                }
                ++pc;
                if(t0m) rt.host->miscStats.record(nowUs()-t0m);
                break;
            }

            case 0xac:
            case 0xad:
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

    runtimeFrame.pc = pc;
    return finish(std::nullopt);
}

std::optional<Value> executeMethod(
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    std::vector<Value>& args,
    Runtime& rt,
    size_t depth) {
    if (depth > kMaxCallDepth) {
        return Value::named("<call-depth-limit>");
    }

    // Skip string alloc for the method label when tracing is off (saves 1 SRAM malloc/call).
    std::string label = rt.trace.recording ? methodLabel(cls, method) : std::string{};
    RuntimeFrame runtimeFrame{std::move(label), &cls, &method, 0, Frame(method.maxLocals, method.maxStack)};
    // Pass args to delegateMethodExecution BEFORE consuming them in the lambda so
    // the timing delegate can read args[1]/args[2] for make_buf logging.
    return delegateMethodExecution(cls, method, args, [&]() {
        initializeFrameArgs(runtimeFrame, args); // moves strings from args into frame locals
        rt.callStack.push_back(std::move(runtimeFrame));
        return resumeCurrentMethod(classes, rt, depth);
    });
}

void resetRuntimeTrace(Runtime& rt) {
    bool recording = rt.trace.recording;
    rt.trace = ExecutionTrace{};
    rt.trace.recording = recording;
    rt.steps = 0;
    rt.callStack.clear();
    rt.graphicsFramebuffer = nullptr;
    rt.graphicsWidth = 0;
    rt.graphicsHeight = 0;
    rt.graphicsColorRgb = 0x000000;
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
        (void)executeMethod(classes, *midletClass, *init, initArgs, rt, 0);
    } else {
        MethodRef ref{midletClass->thisClass, "<init>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {session.midletRef()});
    }

    const ClassFile* startOwner = nullptr;
    const MethodInfo* startApp = findMethodInHierarchy(classes, *midletClass, "startApp", "()V", &startOwner);
    if (startOwner != nullptr && startApp != nullptr) {
        std::vector<Value> startArgs = {session.midletRef()};
        (void)executeMethod(classes, *startOwner, *startApp, startArgs, rt, 0);
    } else {
        MethodRef ref{midletClass->thisClass, "startApp", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {session.midletRef()});
    }

    session.setStarted(true);
    return rt.trace;
}

void dispatchCanvasKeyEvent(MidletSession& session, const HostKeyEvent& event) {
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
    (void)executeMethod(
        classes,
        *owner,
        *handler,
        keyArgs,
        rt,
        0);
    rt.repaintRequested = true;
}

ExecutionTrace renderSession(MidletSession& session, uint16_t* pixels, int width, int height) {
    Runtime& rt = session.runtime();
    resetRuntimeTrace(rt);
    ScopedResourceReadTrace resourceReadTrace(rt.trace);
    rt.graphicsFramebuffer = pixels;
    rt.graphicsWidth = width;
    rt.graphicsHeight = height;

    if (!session.started()) {
        return startSession(session);
    }

    if (rt.host != nullptr) {
        for (const HostKeyEvent& event : rt.host->drainInputEvents()) {
            dispatchCanvasKeyEvent(session, event);
        }
    }

    const uint32_t now = rt.host != nullptr ? rt.host->millis() : 0;
    for (ThreadTask& task : session.tasks()) {
        if (task.finished || now < task.wakeAtMillis) {
            continue;
        }

        rt.currentTask = &task;
        try {
            if (task.suspendedFrame.has_value()) {
                rt.callStack.push_back(std::move(*task.suspendedFrame));
                task.suspendedFrame.reset();
                (void)resumeCurrentMethod(session.classes(), rt, 0);
            } else if (task.cls != nullptr && task.method != nullptr) {
                std::vector<Value> taskArgs = {task.receiver};
                (void)executeMethod(session.classes(), *task.cls, *task.method, taskArgs, rt, 0);
            }
            task.finished = true;
        } catch (const YieldThreadSleep& request) {
            task.wakeAtMillis = now + request.millis;
            if (!rt.callStack.empty()) {
                task.suspendedFrame = std::move(rt.callStack.back());
                rt.callStack.pop_back();
            }
        }
        rt.currentTask = nullptr;
        rt.callStack.clear();
    }

    std::optional<uint32_t> displayableId = objectId(rt.currentDisplayable);
    if (!displayableId.has_value()) {
        captureSuspendedTasks(rt.trace, session);
        return rt.trace;
    }
    auto displayableIt = rt.heap.find(*displayableId);
    if (displayableIt == rt.heap.end()) {
        captureSuspendedTasks(rt.trace, session);
        return rt.trace;
    }
    rt.trace.currentDisplayableClass = displayableIt->second.className;
    captureDisplayableFields(rt.trace, rt, displayableIt->second);

    const std::vector<ClassFile>& classes = session.classes();
    const ClassFile* paintOwner = nullptr;
    const MethodInfo* paint = findMethodInHierarchy(
        classes,
        displayableIt->second.className,
        "paint",
        "(Ljavax/microedition/lcdui/Graphics;)V",
        &paintOwner);
    if (!rt.repaintRequested) {
        captureSuspendedTasks(rt.trace, session);
        return rt.trace;
    }

    if (paintOwner != nullptr && paint != nullptr) {
        std::vector<Value> paintArgs = {rt.currentDisplayable, Value::named("graphics#1")};
        (void)executeMethod(classes, *paintOwner, *paint, paintArgs, rt, 0);
    } else {
        MethodRef ref{displayableIt->second.className, "paint", "(Ljavax/microedition/lcdui/Graphics;)V"};
        (void)recordUnknownCall(rt, "<render>", 0, ref, {rt.currentDisplayable, Value::named("graphics#1")});
    }
    rt.repaintRequested = false;

    captureSuspendedTasks(rt.trace, session);

    return rt.trace;
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
    (void)executeMethod(classes, cls, method, emptyArgs, rt, 0);
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
