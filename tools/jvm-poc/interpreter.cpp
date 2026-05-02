#include "interpreter.hpp"

#include "bytecode.hpp"
#include "jvm_host.hpp"
#include "method_resolution.hpp"
#include "native_methods.hpp"

#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace jvmpoc {
namespace {

constexpr size_t kMaxSteps = 10000;
constexpr size_t kMaxCallDepth = 64;

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

struct HeapObject {
    std::string className;
    std::map<std::string, Value> fields;
};

struct IntArray {
    std::vector<Value> values;
};

using Heap = std::map<uint32_t, HeapObject>;
using IntArrayHeap = std::map<uint32_t, IntArray>;
using StringHeap = std::map<uint32_t, std::string>;

struct RuntimeFrame {
    std::string label;
    Frame frame;
};

struct Runtime {
    const JvmHost* host = nullptr;
    std::map<std::string, Value> staticFields;
    Heap heap;
    IntArrayHeap arrays;
    StringHeap strings;
    std::map<std::string, uint32_t> internedStrings;
    uint32_t nextObjectId = 1;
    uint32_t nextArrayId = 1;
    uint32_t nextStringId = 1;
    std::vector<uint32_t> freeObjectIds;
    std::vector<uint32_t> freeArrayIds;
    std::vector<uint32_t> freeStringIds;
    std::vector<RuntimeFrame> callStack;
    Value displayRef = Value::named("display#1");
    Value currentDisplayable = Value::named("0");
    uint16_t* graphicsPixels = nullptr;
    int graphicsWidth = 0;
    int graphicsHeight = 0;
    uint16_t graphicsColor = 0x0000;
    ExecutionTrace trace;
    size_t steps = 0;
};

Value objectRef(uint32_t id) {
    return Value::named("obj#" + std::to_string(id));
}

Value arrayRef(uint32_t id) {
    return Value::named("arr#" + std::to_string(id));
}

Value stringRef(uint32_t id) {
    return Value::named("str#" + std::to_string(id));
}

Value allocateObject(Runtime& rt, const std::string& label, uint32_t pc, const std::string& className) {
    uint32_t id = 0;
    if (!rt.freeObjectIds.empty()) {
        id = rt.freeObjectIds.back();
        rt.freeObjectIds.pop_back();
    } else {
        id = rt.nextObjectId++;
    }
    rt.heap[id] = HeapObject{className, {}};
    Value ref = objectRef(id);
    rt.trace.objectAllocs.push_back(ObjectAlloc{label, pc, ref, className});
    return ref;
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

std::optional<uint32_t> stringId(const Value& value) {
    return parseHandle(value, "str#");
}

bool isReference(const Value& value) {
    return objectId(value).has_value() || arrayId(value).has_value() || stringId(value).has_value();
}

Value internString(Runtime& rt, const std::string& text) {
    auto internIt = rt.internedStrings.find(text);
    if (internIt != rt.internedStrings.end() && rt.strings.find(internIt->second) != rt.strings.end()) {
        return stringRef(internIt->second);
    }

    uint32_t id = 0;
    if (!rt.freeStringIds.empty()) {
        id = rt.freeStringIds.back();
        rt.freeStringIds.pop_back();
    } else {
        id = rt.nextStringId++;
    }
    rt.strings[id] = text;
    rt.internedStrings[text] = id;
    return stringRef(id);
}

void markValue(
    const Value& value,
    const Runtime& rt,
    std::set<uint32_t>& markedObjects,
    std::set<uint32_t>& markedArrays,
    std::set<uint32_t>& markedStrings) {
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
            markValue(field.second, rt, markedObjects, markedArrays, markedStrings);
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
        for (const Value& element : arrayIt->second.values) {
            markValue(element, rt, markedObjects, markedArrays, markedStrings);
        }
        return;
    }

    std::optional<uint32_t> str = stringId(value);
    if (str.has_value()) {
        markedStrings.insert(*str);
    }
}

void addRoot(
    GcReport& report,
    const std::string& name,
    const Value& value,
    const Runtime& rt,
    std::set<uint32_t>& markedObjects,
    std::set<uint32_t>& markedArrays,
    std::set<uint32_t>& markedStrings) {
    if (!isReference(value)) {
        return;
    }
    report.roots.push_back(name + "=" + value.text);
    markValue(value, rt, markedObjects, markedArrays, markedStrings);
}

void collectGarbage(Runtime& rt, std::string when) {
    GcReport report;
    report.when = std::move(when);

    std::set<uint32_t> markedObjects;
    std::set<uint32_t> markedArrays;
    std::set<uint32_t> markedStrings;
    for (const auto& field : rt.staticFields) {
        addRoot(report, "static " + field.first, field.second, rt, markedObjects, markedArrays, markedStrings);
    }
    for (const RuntimeFrame& runtimeFrame : rt.callStack) {
        const std::vector<Value>& locals = runtimeFrame.frame.locals();
        for (size_t i = 0; i < locals.size(); ++i) {
            addRoot(report, runtimeFrame.label + " local[" + std::to_string(i) + "]",
                    locals[i], rt, markedObjects, markedArrays, markedStrings);
        }

        const std::vector<Value>& stack = runtimeFrame.frame.stack();
        for (size_t i = 0; i < stack.size(); ++i) {
            addRoot(report, runtimeFrame.label + " stack[" + std::to_string(i) + "]",
                    stack[i], rt, markedObjects, markedArrays, markedStrings);
        }
    }

    std::vector<uint32_t> objectsToFree;
    for (const auto& object : rt.heap) {
        if (markedObjects.find(object.first) == markedObjects.end()) {
            report.unreachableObjects.push_back(objectRef(object.first));
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
    std::vector<uint32_t> stringsToFree;
    for (const auto& str : rt.strings) {
        if (markedStrings.find(str.first) == markedStrings.end()) {
            report.unreachableStrings.push_back(stringRef(str.first));
            stringsToFree.push_back(str.first);
        }
    }

    for (uint32_t id : objectsToFree) {
        rt.heap.erase(id);
        rt.freeObjectIds.push_back(id);
        report.freedObjects.push_back(objectRef(id));
    }
    for (uint32_t id : arraysToFree) {
        rt.arrays.erase(id);
        rt.freeArrayIds.push_back(id);
        report.freedArrays.push_back(arrayRef(id));
    }
    for (uint32_t id : stringsToFree) {
        auto strIt = rt.strings.find(id);
        if (strIt != rt.strings.end()) {
            rt.internedStrings.erase(strIt->second);
        }
        rt.strings.erase(id);
        rt.freeStringIds.push_back(id);
        report.freedStrings.push_back(stringRef(id));
    }

    rt.trace.gcReports.push_back(report);
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

    const std::string label = methodLabel(cls, method);
    rt.callStack.push_back(RuntimeFrame{label, Frame(method.maxLocals)});
    Frame& frame = rt.callStack.back().frame;

    auto finish = [&](std::optional<Value> result) -> std::optional<Value> {
        rt.callStack.pop_back();
        return result;
    };

    if (args.empty()) {
        for (size_t i = 0; i < argumentSlots(method) && i < method.maxLocals; ++i) {
            frame.setLocal(static_cast<uint16_t>(i),
                           Value::named("<arg:" + localNameAt(method, static_cast<uint16_t>(i), 0) + ">"));
        }
    } else {
        for (size_t i = 0; i < args.size() && i < method.maxLocals; ++i) {
            frame.setLocal(static_cast<uint16_t>(i), args[i]);
        }
    }

    auto recordLocal = [&](uint16_t index, uint32_t pc, const Value& value, const std::string& reason) {
        rt.trace.localWrites.push_back(LocalWrite{label, pc, index, localNameAt(method, index, pc), value, reason});
    };

    auto store = [&](uint16_t index, uint32_t pc) {
        Value value = frame.pop();
        frame.setLocal(index, value);
        recordLocal(index, pc, value, "store");
    };

    auto makeNativeContext = [&]() {
        return NativeCallContext{
            rt.host,
            rt.trace,
            rt.strings,
            rt.displayRef,
            rt.currentDisplayable,
            rt.graphicsPixels,
            rt.graphicsWidth,
            rt.graphicsHeight,
            rt.graphicsColor,
            [&](std::string when) {
                collectGarbage(rt, std::move(when));
            },
        };
    };

    size_t pc = 0;
    while (pc < method.code.size()) {
        if (++rt.steps > kMaxSteps) {
            rt.trace.stepLimitHit = true;
            return finish(std::nullopt);
        }

        uint8_t op = method.code[pc];
        switch (op) {
            case 0x02: frame.push(Value::named("-1")); ++pc; break;
            case 0x03: frame.push(Value::named("0")); ++pc; break;
            case 0x04: frame.push(Value::named("1")); ++pc; break;
            case 0x05: frame.push(Value::named("2")); ++pc; break;
            case 0x06: frame.push(Value::named("3")); ++pc; break;
            case 0x07: frame.push(Value::named("4")); ++pc; break;
            case 0x08: frame.push(Value::named("5")); ++pc; break;
            case 0x10: frame.push(Value::named(std::to_string(codeS1(method.code, pc + 1)))); pc += 2; break;
            case 0x11: frame.push(Value::named(std::to_string(codeS2(method.code, pc + 1)))); pc += 3; break;
            case 0x12: frame.push(internString(rt, resolveStringConstant(cls, codeU1(method.code, pc + 1)))); pc += 2; break;
            case 0x13: frame.push(internString(rt, resolveStringConstant(cls, codeU2(method.code, pc + 1)))); pc += 3; break;

            case 0x1a: frame.push(frame.local(0)); ++pc; break;
            case 0x1b: frame.push(frame.local(1)); ++pc; break;
            case 0x1c: frame.push(frame.local(2)); ++pc; break;
            case 0x1d: frame.push(frame.local(3)); ++pc; break;
            case 0x15: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;
            case 0x19: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;
            case 0x2a: frame.push(frame.local(0)); ++pc; break;
            case 0x2b: frame.push(frame.local(1)); ++pc; break;
            case 0x2c: frame.push(frame.local(2)); ++pc; break;
            case 0x2d: frame.push(frame.local(3)); ++pc; break;

            case 0x2e: {
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                Value loaded = Value::named("0");
                std::optional<uint32_t> id = arrayId(arrayValue);
                std::optional<int> index = parseIntValue(indexValue);
                if (id.has_value() && index.has_value()) {
                    auto arrayIt = rt.arrays.find(*id);
                    if (arrayIt != rt.arrays.end() && *index >= 0 &&
                        static_cast<size_t>(*index) < arrayIt->second.values.size()) {
                        loaded = arrayIt->second.values[static_cast<size_t>(*index)];
                    }
                }
                frame.push(loaded);
                ++pc;
                break;
            }

            case 0x3b: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3c: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3d: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3e: store(3, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x36: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;
            case 0x3a: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;
            case 0x4b: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4c: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4d: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x4e: store(3, static_cast<uint32_t>(pc)); ++pc; break;

            case 0x4f: {
                uint32_t writePc = static_cast<uint32_t>(pc);
                Value value = frame.pop();
                Value indexValue = frame.pop();
                Value arrayValue = frame.pop();
                std::optional<uint32_t> id = arrayId(arrayValue);
                std::optional<int> index = parseIntValue(indexValue);
                if (id.has_value() && index.has_value()) {
                    auto arrayIt = rt.arrays.find(*id);
                    if (arrayIt != rt.arrays.end() && *index >= 0 &&
                        static_cast<size_t>(*index) < arrayIt->second.values.size()) {
                        arrayIt->second.values[static_cast<size_t>(*index)] = value;
                    }
                }
                rt.trace.arrayWrites.push_back(ArrayWrite{label, writePc, arrayValue, indexValue, value});
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
            case 0x64:
            case 0x68:
            case 0x6c: {
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                const char* opText = op == 0x60 ? "+" : op == 0x64 ? "-" : op == 0x68 ? "*" : "/";
                frame.push(intBinaryOp(lhs, rhs, opText, op));
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

                NativeCallContext nativeCtx = makeNativeContext();
                NativeCallResult nativeResult = handleNativeStaticCall(nativeCtx, label, callPc, ref, callArgs);
                if (nativeResult.handled) {
                    if (nativeResult.returnValue.has_value()) {
                        frame.push(*nativeResult.returnValue);
                    }
                } else {
                    const ClassFile* targetClass = nullptr;
                    const MethodInfo* targetMethod = findMethodInHierarchy(
                        classes, ref.className, ref.name, ref.descriptor, &targetClass);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, callArgs, rt, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    } else {
                        std::optional<Value> result = recordUnknownCall(rt, label, callPc, ref, callArgs);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    }
                }
                pc += 3;
                break;
            }

            case 0xb6:
            case 0xb7: {
                MethodRef ref = resolveMethodRef(cls, codeU2(method.code, pc + 1));
                std::vector<size_t> widths = argumentSlotWidths(ref.descriptor);
                std::vector<Value> callArgs(widths.size() + 1);
                for (size_t i = widths.size(); i > 0; --i) {
                    callArgs[i] = frame.pop();
                }
                Value object = frame.pop();
                callArgs[0] = object;

                NativeCallContext nativeCtx = makeNativeContext();
                NativeCallResult nativeResult = handleNativeInstanceCall(
                    nativeCtx, label, static_cast<uint32_t>(pc), ref, callArgs);
                if (nativeResult.handled) {
                    if (nativeResult.returnValue.has_value()) {
                        frame.push(*nativeResult.returnValue);
                    }
                    pc += 3;
                    break;
                }

                std::string lookupClassName = ref.className;
                if (op == 0xb6) {
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
                    std::optional<Value> result = executeMethod(
                        classes, *targetClass, *targetMethod, callArgs, rt, depth + 1);
                    if (result.has_value()) {
                        frame.push(*result);
                    }
                } else {
                    std::optional<Value> result = recordUnknownCall(rt, label, static_cast<uint32_t>(pc), ref, callArgs);
                    if (result.has_value()) {
                        frame.push(*result);
                    }
                }
                pc += 3;
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
                if (atype == 10 && count.has_value() && *count >= 0) {
                    uint32_t id = 0;
                    if (!rt.freeArrayIds.empty()) {
                        id = rt.freeArrayIds.back();
                        rt.freeArrayIds.pop_back();
                    } else {
                        id = rt.nextArrayId++;
                    }
                    rt.arrays[id] = IntArray{std::vector<Value>(static_cast<size_t>(*count), Value::named("0"))};
                    Value ref = arrayRef(id);
                    rt.trace.arrayAllocs.push_back(ArrayAlloc{label, allocPc, ref, static_cast<size_t>(*count)});
                    frame.push(ref);
                } else {
                    frame.push(Value::named("<array>"));
                }
                pc += 2;
                break;
            }

            case 0xbe:
            {
                Value arrayValue = frame.pop();
                std::optional<uint32_t> id = arrayId(arrayValue);
                auto arrayIt = id.has_value() ? rt.arrays.find(*id) : rt.arrays.end();
                if (id.has_value() && arrayIt != rt.arrays.end()) {
                    frame.push(Value::named(std::to_string(arrayIt->second.values.size())));
                } else {
                    frame.push(Value::named("<arraylength:" + arrayValue.text + ">"));
                }
                ++pc;
                break;
            }

            case 0xac:
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

} // namespace

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
    Runtime rt;
    rt.host = host;
    rt.callStack.reserve(kMaxCallDepth + 1);

    const ClassFile* midletClass = findClass(classes, className);
    if (midletClass == nullptr) {
        MethodRef missing{className, "<load>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, missing, {});
        return rt.trace;
    }

    Value midlet = allocateObject(rt, "<midlet>", 0, midletClass->thisClass);
    const MethodInfo* init = findDeclaredMethod(*midletClass, "<init>", "()V");
    if (init != nullptr) {
        (void)executeMethod(classes, *midletClass, *init, {midlet}, rt, 0);
    } else {
        MethodRef ref{midletClass->thisClass, "<init>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {midlet});
    }

    const ClassFile* startOwner = nullptr;
    const MethodInfo* startApp = findMethodInHierarchy(classes, *midletClass, "startApp", "()V", &startOwner);
    if (startOwner != nullptr && startApp != nullptr) {
        (void)executeMethod(classes, *startOwner, *startApp, {midlet}, rt, 0);
    } else {
        MethodRef ref{midletClass->thisClass, "startApp", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, ref, {midlet});
    }

    return rt.trace;
}

ExecutionTrace renderMidletFrame(
    const std::vector<ClassFile>& classes,
    const std::string& className,
    const JvmHost* host,
    uint16_t* pixels,
    int width,
    int height) {
    Runtime rt;
    rt.host = host;
    rt.graphicsPixels = pixels;
    rt.graphicsWidth = width;
    rt.graphicsHeight = height;
    rt.callStack.reserve(kMaxCallDepth + 1);

    const ClassFile* midletClass = findClass(classes, className);
    if (midletClass == nullptr) {
        MethodRef missing{className, "<load>", "()V"};
        (void)recordUnknownCall(rt, "<midlet>", 0, missing, {});
        return rt.trace;
    }

    Value midlet = allocateObject(rt, "<midlet>", 0, midletClass->thisClass);
    const MethodInfo* init = findDeclaredMethod(*midletClass, "<init>", "()V");
    if (init != nullptr) {
        (void)executeMethod(classes, *midletClass, *init, {midlet}, rt, 0);
    }

    const ClassFile* startOwner = nullptr;
    const MethodInfo* startApp = findMethodInHierarchy(classes, *midletClass, "startApp", "()V", &startOwner);
    if (startOwner != nullptr && startApp != nullptr) {
        (void)executeMethod(classes, *startOwner, *startApp, {midlet}, rt, 0);
    }

    std::optional<uint32_t> displayableId = objectId(rt.currentDisplayable);
    if (!displayableId.has_value()) {
        return rt.trace;
    }
    auto displayableIt = rt.heap.find(*displayableId);
    if (displayableIt == rt.heap.end()) {
        return rt.trace;
    }

    const ClassFile* paintOwner = nullptr;
    const MethodInfo* paint = findMethodInHierarchy(
        classes,
        displayableIt->second.className,
        "paint",
        "(Ljavax/microedition/lcdui/Graphics;)V",
        &paintOwner);
    if (paintOwner != nullptr && paint != nullptr) {
        (void)executeMethod(classes, *paintOwner, *paint, {rt.currentDisplayable, Value::named("graphics#1")}, rt, 0);
    } else {
        MethodRef ref{displayableIt->second.className, "paint", "(Ljavax/microedition/lcdui/Graphics;)V"};
        (void)recordUnknownCall(rt, "<render>", 0, ref, {rt.currentDisplayable, Value::named("graphics#1")});
    }

    return rt.trace;
}

} // namespace jvmpoc
