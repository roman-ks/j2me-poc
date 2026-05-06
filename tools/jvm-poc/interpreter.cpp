#include "interpreter.hpp"

#include "bytecode.hpp"
#include "jvm_host.hpp"
#include "method_execution_delegate.hpp"
#include "method_resolution.hpp"
#include "native_methods.hpp"
#include "j2me_port/J2MECompat.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
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

std::string compareText(const Value& lhs, const char* op, const Value& rhs) {
    return lhs.text + " " + op + " " + rhs.text;
}

Value intBinaryOp(const Value& lhs, const Value& rhs, const char* op, uint8_t opcode) {
    std::optional<int> left = parseIntValue(lhs);
    std::optional<int> right = parseIntValue(rhs);
    if (left && right) {
        switch (opcode) {
            case 0x60: return Value::named(std::to_string(*left + *right));
            case 0x64: return Value::named(std::to_string(*left - *right));
            case 0x68: return Value::named(std::to_string(*left * *right));
            case 0x6c:
                if (*right == 0) {
                    return Value::named("<divide-by-zero>");
                }
                return Value::named(std::to_string(*left / *right));
            default:
                break;
        }
    }
    return Value::named("(" + lhs.text + " " + op + " " + rhs.text + ")");
}

Value longBinaryOp(const Value& lhs, const Value& rhs, const char* op, uint8_t opcode) {
    std::optional<long long> left = parseLongValue(lhs);
    std::optional<long long> right = parseLongValue(rhs);
    if (left && right) {
        switch (opcode) {
            case 0x61: return Value::named(std::to_string(*left + *right));
            case 0x65: return Value::named(std::to_string(*left - *right));
            case 0x69: return Value::named(std::to_string(*left * *right));
            case 0x6d:
                if (*right == 0) {
                    return Value::named("<divide-by-zero>");
                }
                return Value::named(std::to_string(*left / *right));
            default:
                break;
        }
    }
    return Value::named("(" + lhs.text + " " + op + " " + rhs.text + ")");
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

struct HeapObject {
    std::string className;
    std::map<std::string, Value> fields;
};

using Heap = std::map<uint32_t, HeapObject>;
using ArrayHeap = std::map<uint32_t, std::vector<Value>>;
using StringHeap = std::map<uint32_t, std::string>;
using ImageHeap = std::map<uint32_t, port::Image>;
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
    std::map<std::string, Value> staticFields;
    Heap heap;
    ArrayHeap arrays;
    StringHeap strings;
    ImageHeap images;
    ResourceImageCache resourceImages;
    std::map<std::string, uint32_t> internedStrings;
    uint32_t nextObjectId = 1;
    uint32_t nextArrayId = 1;
    uint32_t nextImageId = 1;
    std::vector<uint32_t> freeObjectIds;
    std::vector<uint32_t> freeArrayIds;
    std::vector<RuntimeFrame> callStack;
    Value displayRef = Value::named("display#1");
    Value currentDisplayable = Value::named("0");
    std::optional<std::reference_wrapper<std::vector<uint16_t>>> graphicsFramebuffer;
    int graphicsWidth = 0;
    int graphicsHeight = 0;
    int graphicsColorRgb = 0x000000;
    ExecutionTrace trace;
    size_t steps = 0;
    bool repaintRequested = true;
};

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

void captureStackSnapshot(ExecutionTrace& trace, const std::vector<RuntimeFrame>& callStack) {
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
    return Value::named("obj#" + std::to_string(id));
}

Value arrayRef(uint32_t id) {
    return Value::named("arr#" + std::to_string(id));
}

std::optional<std::string> parseTextHandle(const Value& value, const std::string& prefix) {
    if (value.text.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    return value.text.substr(prefix.size());
}

uint32_t allocateHeapObjectId(Runtime& rt, const std::string& className) {
    uint32_t id = 0;
    if (!rt.freeObjectIds.empty()) {
        id = rt.freeObjectIds.back();
        rt.freeObjectIds.pop_back();
    } else {
        id = rt.nextObjectId++;
    }
    rt.heap[id] = HeapObject{className, {}};
    return id;
}

Value allocateObject(Runtime& rt, const std::string& label, uint32_t pc, const std::string& className) {
    uint32_t id = allocateHeapObjectId(rt, className);
    Value ref = objectRef(id);
    rt.trace.objectAllocs.push_back(ObjectAlloc{label, pc, ref, className});
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
    rt.arrays[id] = std::vector<Value>(length, Value::named("0"));
    Value ref = arrayRef(id);
    rt.trace.arrayAllocs.push_back(ArrayAlloc{label, pc, ref, length});
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
    Value loaded = Value::named("0");
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
    return Value::named(std::to_string(static_cast<int>(static_cast<int8_t>(*parsed))));
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
    rt.trace.arrayWrites.push_back(ArrayWrite{label, pc, arrayValue, indexValue, value});
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
        std::string path = args.size() > 1 ? runtimeString(rt, args[1]).value_or(args[1].text) : "";
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
        result.has_value() ? result->text : "",
    });
    return result;
}

std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix) {
    if (value.text.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.text.c_str() + prefix.size(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> objectId(const Value& value) {
    return parseHandle(value, "obj#");
}

std::optional<uint32_t> arrayId(const Value& value) {
    return parseHandle(value, "arr#");
}

bool isReference(const Value& value) {
    return objectId(value).has_value() || arrayId(value).has_value();
}

std::string debugValueText(const Runtime& rt, const Value& value) {
    std::optional<uint32_t> id = objectId(value);
    if (id.has_value()) {
        auto stringIt = rt.strings.find(*id);
        if (stringIt != rt.strings.end()) {
            return '"' + stringIt->second + '"';
        }
    }
    return value.text;
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
        auto fieldIt = object.fields.find(fieldName);
        if (fieldIt == object.fields.end()) {
            continue;
        }
        trace.currentDisplayableFields.push_back(
            std::string(fieldName) + "=" + debugValueText(rt, fieldIt->second));
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
        for (const auto& field : objectIt->second.fields) {
            markValue(field.second, rt, markedObjects, markedArrays);
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
    report.roots.push_back(name + "=" + value.text);
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
        const std::vector<Value>& locals = runtimeFrame.frame.locals();
        for (size_t i = 0; i < locals.size(); ++i) {
            addRoot(report, runtimeFrame.label + " local[" + std::to_string(i) + "]", locals[i], rt, markedObjects, markedArrays);
        }

        const std::vector<Value>& stack = runtimeFrame.frame.stack();
        for (size_t i = 0; i < stack.size(); ++i) {
            addRoot(report, runtimeFrame.label + " stack[" + std::to_string(i) + "]", stack[i], rt, markedObjects, markedArrays);
        }
    }
    if (rt.session != nullptr) {
        for (const ThreadTask& task : rt.session->tasks()) {
            if (!task.suspendedFrame.has_value()) {
                continue;
            }
            const std::vector<Value>& locals = task.suspendedFrame->frame.locals();
            for (size_t i = 0; i < locals.size(); ++i) {
                addRoot(report, task.suspendedFrame->label + " local[" + std::to_string(i) + "]", locals[i], rt, markedObjects, markedArrays);
            }
            const std::vector<Value>& stack = task.suspendedFrame->frame.stack();
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
    if (!id.has_value()) {
        return Value::named("0");
    }
    auto objectIt = rt.heap.find(*id);
    if (objectIt == rt.heap.end()) {
        return Value::named("0");
    }
    auto fieldIt = objectIt->second.fields.find(fieldName);
    return fieldIt == objectIt->second.fields.end() ? Value::named("0") : fieldIt->second;
}

std::optional<Value> executeMethod(
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    const std::vector<Value>& args,
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
    (void)executeMethod(classes, *owner, *run, {runnable}, rt, 0);
}

void initializeFrameArgs(RuntimeFrame& runtimeFrame, const std::vector<Value>& args) {
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
        frame.setLocal(0, args[0]);
        argIndex = 1;
    }
    std::vector<size_t> widths = argumentSlotWidths(method.descriptor);
    for (size_t i = 0; i < widths.size() && argIndex < args.size() && localIndex < method.maxLocals; ++i) {
        frame.setLocal(static_cast<uint16_t>(localIndex), args[argIndex]);
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
    size_t& pc = runtimeFrame.pc;

    auto finish = [&](std::optional<Value> result) -> std::optional<Value> {
        rt.callStack.pop_back();
        return result;
    };

    auto recordLocal = [&](uint16_t index, uint32_t writePc, const Value& value, const std::string& reason) {
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

    while (pc < method.code.size()) {
        if (++rt.steps > kMaxSteps) {
            rt.trace.stepLimitHit = true;
            captureStackSnapshot(rt.trace, rt.callStack);
            return finish(std::nullopt);
        }

        uint8_t op = method.code[pc];
        switch (op) {
            case 0x01: frame.push(Value::named("0")); ++pc; break;
            case 0x02: frame.push(Value::named("-1")); ++pc; break;
            case 0x03: frame.push(Value::named("0")); ++pc; break;
            case 0x04: frame.push(Value::named("1")); ++pc; break;
            case 0x05: frame.push(Value::named("2")); ++pc; break;
            case 0x06: frame.push(Value::named("3")); ++pc; break;
            case 0x07: frame.push(Value::named("4")); ++pc; break;
            case 0x08: frame.push(Value::named("5")); ++pc; break;
            case 0x09: frame.push(Value::named("0")); ++pc; break;
            case 0x0a: frame.push(Value::named("1")); ++pc; break;
            case 0x10: frame.push(Value::named(std::to_string(codeS1(method.code, pc + 1)))); pc += 2; break;
            case 0x11: frame.push(Value::named(std::to_string(codeS2(method.code, pc + 1)))); pc += 3; break;
            case 0x12: {
                uint16_t index = codeU1(method.code, pc + 1);
                frame.push(cls.cp[index].tag == CpInteger
                    ? Value::named(std::to_string(resolveIntegerConstant(cls, index)))
                    : internString(rt, resolveStringConstant(cls, index)));
                pc += 2;
                break;
            }
            case 0x13: {
                uint16_t index = codeU2(method.code, pc + 1);
                frame.push(cls.cp[index].tag == CpInteger
                    ? Value::named(std::to_string(resolveIntegerConstant(cls, index)))
                    : internString(rt, resolveStringConstant(cls, index)));
                pc += 3;
                break;
            }
            case 0x14: {
                uint16_t index = codeU2(method.code, pc + 1);
                frame.push(Value::named(std::to_string(resolveLongConstant(cls, index))));
                pc += 3;
                break;
            }

            case 0x1e: frame.push(frame.local(0)); ++pc; break;
            case 0x1f: frame.push(frame.local(1)); ++pc; break;
            case 0x20: frame.push(frame.local(2)); ++pc; break;
            case 0x21: frame.push(frame.local(3)); ++pc; break;
            case 0x1a: frame.push(frame.local(0)); ++pc; break;
            case 0x1b: frame.push(frame.local(1)); ++pc; break;
            case 0x1c: frame.push(frame.local(2)); ++pc; break;
            case 0x1d: frame.push(frame.local(3)); ++pc; break;
            case 0x16: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;
            case 0x15: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;
            case 0x19: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;
            case 0x2a: frame.push(frame.local(0)); ++pc; break;
            case 0x2b: frame.push(frame.local(1)); ++pc; break;
            case 0x2c: frame.push(frame.local(2)); ++pc; break;
            case 0x2d: frame.push(frame.local(3)); ++pc; break;

            case 0x2e:
            case 0x32:
            case 0x33:
            case 0x34: {
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                frame.push(loadArrayElement(rt, arrayValue, indexValue));
                ++pc;
                break;
            }

            case 0x3f: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x40: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x41: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x42: store(3, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3b: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3c: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3d: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3e: store(3, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x37: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;
            case 0x36: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;
            case 0x3a: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;
            case 0x4b: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4c: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4d: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4e: store(3, static_cast<uint32_t>(pc)); ++pc; break;

            case 0x4f:
            case 0x53:
            case 0x54:
            case 0x55: {
                uint32_t writePc = static_cast<uint32_t>(pc);
                Value value = frame.pop();
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                storeArrayElement(rt, label, writePc, arrayValue, indexValue, value, op == 0x54);
                ++pc;
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

            case 0x57:
                (void)frame.pop();
                ++pc;
                break;

            case 0x84: {
                uint32_t iincPc = static_cast<uint32_t>(pc);
                uint16_t index = codeU1(method.code, pc + 1);
                int delta = codeS1(method.code, pc + 2);
                Value oldValue = frame.local(index);
                std::optional<int> oldInt = parseIntValue(oldValue);
                Value newValue = oldInt
                    ? Value::named(std::to_string(*oldInt + delta))
                    : Value::named("(" + oldValue.text + " + " + std::to_string(delta) + ")");
                frame.setLocal(index, newValue);
                recordLocal(index, iincPc, newValue, "iinc");
                pc += 3;
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
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                const char* opText = (op == 0x60 || op == 0x61) ? "+" :
                    (op == 0x64 || op == 0x65) ? "-" :
                    (op == 0x68 || op == 0x69) ? "*" :
                    (op == 0x6c || op == 0x6d) ? "/" : "%";
                std::optional<int> left = parseIntValue(lhs);
                std::optional<int> right = parseIntValue(rhs);
                if (op == 0x70 && left.has_value() && right.has_value()) {
                    frame.push(*right == 0 ? Value::named("<divide-by-zero>") : Value::named(std::to_string(*left % *right)));
                } else if (op == 0x71) {
                    std::optional<long long> leftLong = parseLongValue(lhs);
                    std::optional<long long> rightLong = parseLongValue(rhs);
                    frame.push(leftLong.has_value() && rightLong.has_value() && *rightLong != 0
                        ? Value::named(std::to_string(*leftLong % *rightLong))
                        : leftLong.has_value() && rightLong.has_value() && *rightLong == 0
                            ? Value::named("<divide-by-zero>")
                            : Value::named("(" + lhs.text + " % " + rhs.text + ")"));
                } else if (op == 0x61 || op == 0x65 || op == 0x69 || op == 0x6d) {
                    frame.push(longBinaryOp(lhs, rhs, opText, op));
                } else {
                    frame.push(intBinaryOp(lhs, rhs, opText, op));
                }
                ++pc;
                break;
            }

            case 0x74: {
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                frame.push(parsed.has_value()
                    ? Value::named(std::to_string(-*parsed))
                    : Value::named("(-" + value.text + ")"));
                ++pc;
                break;
            }

            case 0x75: {
                Value value = frame.pop();
                std::optional<long long> parsed = parseLongValue(value);
                frame.push(parsed.has_value()
                    ? Value::named(std::to_string(-*parsed))
                    : Value::named("(-" + value.text + ")"));
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
                    ? Value::named(*left < *right ? "-1" : *left > *right ? "1" : "0")
                    : Value::named("<lcmp:" + lhs.text + "," + rhs.text + ">"));
                ++pc;
                break;
            }

            case 0x99:
            case 0x9a:
            case 0x9b:
            case 0x9c:
            case 0x9d:
            case 0x9e: {
                uint32_t branchPc = static_cast<uint32_t>(pc);
                int16_t offset = codeS2(method.code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value value = frame.pop();
                std::optional<int> parsed = parseIntValue(value);
                bool known = parsed.has_value();
                bool taken = known && compareIntWithZero(*parsed, op);
                rt.trace.branches.push_back(BranchTrace{
                    label,
                    branchPc,
                    compareText(value, compareZeroOpText(op), Value::named("0")),
                    known,
                    taken,
                    target,
                });
                pc = taken ? target : pc + 3;
                break;
            }

            case 0x9f:
            case 0xa0:
            case 0xa1:
            case 0xa2:
            case 0xa3:
            case 0xa4: {
                uint32_t branchPc = static_cast<uint32_t>(pc);
                int16_t offset = codeS2(method.code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                std::optional<int> left = parseIntValue(lhs);
                std::optional<int> right = parseIntValue(rhs);
                bool known = left.has_value() && right.has_value();
                bool taken = known && compareInts(*left, *right, op);
                rt.trace.branches.push_back(BranchTrace{
                    label,
                    branchPc,
                    compareText(lhs, compareOpText(op), rhs),
                    known,
                    taken,
                    target,
                });
                pc = taken ? target : pc + 3;
                break;
            }

            case 0xa7:
                pc = branchTarget(pc, codeS2(method.code, pc + 1));
                break;

            case 0xc6:
            case 0xc7: {
                uint32_t branchPc = static_cast<uint32_t>(pc);
                int16_t offset = codeS2(method.code, pc + 1);
                uint32_t target = branchTarget(pc, offset);
                Value value = frame.pop();
                bool known = value.text == "0";
                bool taken = op == 0xc6 ? known : !known;
                rt.trace.branches.push_back(BranchTrace{
                    label,
                    branchPc,
                    value.text + (op == 0xc6 ? " == null" : " != null"),
                    true,
                    taken,
                    target,
                });
                pc = taken ? target : pc + 3;
                break;
            }

            case 0xb2:
            {
                FieldRef ref = resolveFieldRef(cls, codeU2(method.code, pc + 1));
                std::string key = ref.className + "." + ref.name;
                auto it = rt.staticFields.find(key);
                frame.push(it == rt.staticFields.end() ? Value::named("0") : it->second);
                pc += 3;
                break;
            }

            case 0xb3:
            {
                uint32_t writePc = static_cast<uint32_t>(pc);
                FieldRef ref = resolveFieldRef(cls, codeU2(method.code, pc + 1));
                std::string key = ref.className + "." + ref.name;
                Value value = frame.pop();
                rt.staticFields[key] = value;
                rt.trace.staticWrites.push_back(StaticWrite{label, writePc, key, value});
                pc += 3;
                break;
            }

            case 0xb4:
            {
                FieldRef ref = resolveFieldRef(cls, codeU2(method.code, pc + 1));
                Value object = frame.pop();
                std::optional<uint32_t> id = objectId(object);
                Value value = Value::named("0");
                if (id.has_value()) {
                    auto objectIt = rt.heap.find(*id);
                    if (objectIt != rt.heap.end()) {
                        auto fieldIt = objectIt->second.fields.find(ref.name);
                        if (fieldIt != objectIt->second.fields.end()) {
                            value = fieldIt->second;
                        }
                    }
                }
                frame.push(value);
                pc += 3;
                break;
            }

            case 0xb5:
            {
                uint32_t writePc = static_cast<uint32_t>(pc);
                FieldRef ref = resolveFieldRef(cls, codeU2(method.code, pc + 1));
                Value value = frame.pop();
                Value object = frame.pop();
                std::optional<uint32_t> id = objectId(object);
                if (id.has_value()) {
                    rt.heap[*id].fields[ref.name] = value;
                }
                rt.trace.fieldWrites.push_back(FieldWrite{label, writePc, object, ref.className + "." + ref.name, value});
                pc += 3;
                break;
            }

            case 0xb8: {
                uint32_t callPc = static_cast<uint32_t>(pc);
                MethodRef ref = resolveMethodRef(cls, codeU2(method.code, pc + 1));
                std::vector<size_t> widths = argumentSlotWidths(ref.descriptor);
                std::vector<Value> callArgs(widths.size());
                for (size_t i = widths.size(); i > 0; --i) {
                    callArgs[i - 1] = frame.pop();
                }

                const ClassFile* targetClass = nullptr;
                const MethodInfo* targetMethod = findMethodInHierarchy(
                    classes, ref.className, ref.name, ref.descriptor, &targetClass);
                if (targetClass != nullptr && targetMethod != nullptr) {
                    if (hasAccess(targetMethod->access, kAccNative)) {
                        NativeCallContext nativeCtx = makeNativeContext();
                        NativeCallResult nativeResult;
                        try {
                            nativeResult = handleNativeStaticCall(
                                nativeCtx, label, callPc, methodRefForOwner(*targetClass, ref), callArgs);
                        } catch (const YieldThreadSleep&) {
                            pc += 3;
                            throw;
                        }
                        if (nativeResult.handled) {
                            if (nativeResult.returnValue.has_value()) {
                                frame.push(*nativeResult.returnValue);
                            }
                        } else {
                            std::optional<Value> result = recordUnknownCall(
                                rt, label, callPc, methodRefForOwner(*targetClass, ref), callArgs);
                            if (result.has_value()) {
                                frame.push(*result);
                            }
                        }
                    } else {
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, callArgs, rt, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                } else {
                    std::optional<Value> result = recordUnknownCall(rt, label, callPc, ref, callArgs);
                    if (result.has_value()) {
                        frame.push(*result);
                    }
                }
                pc += 3;
                break;
            }

            case 0xb6:
            case 0xb7:
            case 0xb9: {
                MethodRef ref = resolveMethodRef(cls, codeU2(method.code, pc + 1));
                std::vector<size_t> widths = argumentSlotWidths(ref.descriptor);
                std::vector<Value> callArgs(widths.size() + 1);
                for (size_t i = widths.size(); i > 0; --i) {
                    callArgs[i] = frame.pop();
                }
                Value object = frame.pop();
                callArgs[0] = object;

                std::string lookupClassName = ref.className;
                if (op == 0xb6 || op == 0xb9) {
                    std::optional<uint32_t> id = objectId(object);
                    if (id.has_value()) {
                        auto objectIt = rt.heap.find(*id);
                        if (objectIt != rt.heap.end()) {
                            lookupClassName = objectIt->second.className;
                        }
                    }
                }
                const ClassFile* targetClass = nullptr;
                const MethodInfo* targetMethod = findMethodInHierarchy(
                    classes, lookupClassName, ref.name, ref.descriptor, &targetClass);
                if (targetClass != nullptr && targetMethod != nullptr) {
                    if (hasAccess(targetMethod->access, kAccNative)) {
                        NativeCallContext nativeCtx = makeNativeContext();
                        std::optional<uint32_t> nativeObjectId = objectId(object);
                        if (nativeObjectId.has_value()) {
                            auto objectIt = rt.heap.find(*nativeObjectId);
                            if (objectIt != rt.heap.end()) {
                                nativeCtx.receiverClassName = objectIt->second.className;
                            }
                        }
                        NativeCallResult nativeResult;
                        try {
                            nativeResult = handleNativeInstanceCall(
                                nativeCtx, label, static_cast<uint32_t>(pc), methodRefForOwner(*targetClass, ref), callArgs);
                        } catch (const YieldThreadSleep&) {
                            pc += op == 0xb9 ? 5 : 3;
                            throw;
                        }
                        if (nativeResult.handled) {
                            if (nativeResult.returnValue.has_value()) {
                                frame.push(*nativeResult.returnValue);
                            }
                        } else {
                            std::optional<Value> result = recordUnknownCall(
                                rt, label, static_cast<uint32_t>(pc), methodRefForOwner(*targetClass, ref), callArgs);
                            if (result.has_value()) {
                                frame.push(*result);
                            }
                        }
                    } else {
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, callArgs, rt, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                } else {
                    NativeCallResult builtInResult = handleBuiltInInstanceCall(rt, ref, callArgs);
                    if (builtInResult.handled) {
                        if (builtInResult.returnValue.has_value()) {
                            frame.push(*builtInResult.returnValue);
                        }
                    } else {
                        std::optional<Value> result = recordUnknownCall(rt, label, static_cast<uint32_t>(pc), ref, callArgs);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                }
                pc += op == 0xb9 ? 5 : 3;
                break;
            }

            case 0xbb:
            {
                uint32_t allocPc = static_cast<uint32_t>(pc);
                std::string className = resolveClassRef(cls, codeU2(method.code, pc + 1));
                frame.push(allocateObject(rt, label, allocPc, className));
                pc += 3;
                break;
            }

            case 0xbc:
            {
                uint32_t allocPc = static_cast<uint32_t>(pc);
                uint8_t atype = codeU1(method.code, pc + 1);
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
                uint8_t dimensions = codeU1(method.code, pc + 3);
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
                Value arrayValue = frame.pop();
                std::optional<uint32_t> id = arrayId(arrayValue);
                auto arrayIt = id.has_value() ? rt.arrays.find(*id) : rt.arrays.end();
                if (id.has_value() && arrayIt != rt.arrays.end()) {
                    frame.push(Value::named(std::to_string(arrayIt->second.size())));
                } else {
                    frame.push(Value::named("<arraylength:" + arrayValue.text + ">"));
                }
                ++pc;
                break;
            }

            case 0xac:
            case 0xad:
            case 0xb0:
                return finish(frame.pop());

            case 0xb1:
                return finish(std::nullopt);

            default:
                pc += instructionLength(op);
                break;
        }
    }

    return finish(std::nullopt);
}

std::optional<Value> executeMethod(
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    const std::vector<Value>& args,
    Runtime& rt,
    size_t depth) {
    if (depth > kMaxCallDepth) {
        return Value::named("<call-depth-limit>");
    }

    RuntimeFrame runtimeFrame{methodLabel(cls, method), &cls, &method, 0, Frame(method.maxLocals)};
    initializeFrameArgs(runtimeFrame, args);
    rt.callStack.push_back(std::move(runtimeFrame));
    return delegateMethodExecution(cls, method, args, [&]() {
        return resumeCurrentMethod(classes, rt, depth);
    });
}

void resetRuntimeTrace(Runtime& rt) {
    rt.trace = ExecutionTrace{};
    rt.steps = 0;
    rt.callStack.clear();
    rt.graphicsFramebuffer.reset();
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

    session.midletRef() = allocateObject(rt, "<midlet>", 0, midletClass->thisClass);
    const MethodInfo* init = findDeclaredMethod(*midletClass, "<init>", "()V");
    if (init != nullptr) {
        (void)executeMethod(classes, *midletClass, *init, {session.midletRef()}, rt, 0);
    } else {
        MethodRef ref{midletClass->thisClass, "<init>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {session.midletRef()});
    }

    const ClassFile* startOwner = nullptr;
    const MethodInfo* startApp = findMethodInHierarchy(classes, *midletClass, "startApp", "()V", &startOwner);
    if (startOwner != nullptr && startApp != nullptr) {
        (void)executeMethod(classes, *startOwner, *startApp, {session.midletRef()}, rt, 0);
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

    (void)executeMethod(
        classes,
        *owner,
        *handler,
        {rt.currentDisplayable, Value::named(std::to_string(event.keyCode))},
        rt,
        0);
    rt.repaintRequested = true;
}

ExecutionTrace renderSession(MidletSession& session, std::vector<uint16_t>& pixels, int width, int height) {
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
                (void)executeMethod(session.classes(), *task.cls, *task.method, {task.receiver}, rt, 0);
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
        (void)executeMethod(classes, *paintOwner, *paint, {rt.currentDisplayable, Value::named("graphics#1")}, rt, 0);
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

ExecutionTrace renderMidletSession(
    MidletSession& session,
    std::vector<uint16_t>& pixels,
    int width,
    int height) {
    return renderSession(session, pixels, width, height);
}

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method) {
    Runtime rt;
    rt.callStack.reserve(kMaxCallDepth + 1);
    (void)executeMethod(classes, cls, method, {}, rt, 0);
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
    std::vector<uint16_t>& pixels,
    int width,
    int height) {
    std::shared_ptr<MidletSession> session = createMidletSession(classes, className, host);
    (void)startMidletSession(*session);
    return renderMidletSession(*session, pixels, width, height);
}

} // namespace jvmpoc
