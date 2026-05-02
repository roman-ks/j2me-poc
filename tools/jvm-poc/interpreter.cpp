#include "interpreter.hpp"

#include "bytecode.hpp"

#include <cstdint>
#include <optional>
#include <string>
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

bool isNativeRuntimePrintInt(const MethodRef& ref) {
    const std::string suffix = "/NativeRuntime";
    bool classMatches = ref.className == "NativeRuntime";
    if (ref.className.size() >= suffix.size()) {
        classMatches = classMatches ||
                       ref.className.compare(ref.className.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    return classMatches && ref.name == "printInt" && ref.descriptor == "(I)V";
}

std::string methodLabel(const ClassFile& cls, const MethodInfo& method) {
    return cls.thisClass + "." + method.name + method.descriptor;
}

const ClassFile* findClass(const std::vector<ClassFile>& classes, const std::string& name) {
    for (const ClassFile& cls : classes) {
        if (cls.thisClass == name) {
            return &cls;
        }
    }
    return nullptr;
}

const MethodInfo* findMethod(const ClassFile& cls, const MethodRef& ref) {
    for (const MethodInfo& method : cls.methods) {
        if (method.name == ref.name && method.descriptor == ref.descriptor) {
            return &method;
        }
    }
    return nullptr;
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

std::optional<Value> executeMethod(
    const std::vector<ClassFile>& classes,
    const ClassFile& cls,
    const MethodInfo& method,
    const std::vector<Value>& args,
    ExecutionTrace& trace,
    size_t& steps,
    size_t depth) {
    if (depth > kMaxCallDepth) {
        return Value::named("<call-depth-limit>");
    }

    Frame frame(method.maxLocals);
    const std::string label = methodLabel(cls, method);

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
        trace.localWrites.push_back(LocalWrite{label, pc, index, localNameAt(method, index, pc), value, reason});
    };

    auto store = [&](uint16_t index, uint32_t pc) {
        Value value = frame.pop();
        frame.setLocal(index, value);
        recordLocal(index, pc, value, "store");
    };

    size_t pc = 0;
    while (pc < method.code.size()) {
        if (++steps > kMaxSteps) {
            trace.stepLimitHit = true;
            return std::nullopt;
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

            case 0x1a: frame.push(frame.local(0)); ++pc; break;
            case 0x1b: frame.push(frame.local(1)); ++pc; break;
            case 0x1c: frame.push(frame.local(2)); ++pc; break;
            case 0x1d: frame.push(frame.local(3)); ++pc; break;
            case 0x15: frame.push(frame.local(codeU1(method.code, pc + 1))); pc += 2; break;

            case 0x3b: store(0, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3c: store(1, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3d: store(2, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x3e: store(3, static_cast<uint32_t>(pc)); ++pc; break;
            case 0x36: store(codeU1(method.code, pc + 1), static_cast<uint32_t>(pc)); pc += 2; break;

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
                trace.branches.push_back(BranchTrace{
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
                trace.branches.push_back(BranchTrace{
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
                frame.push(Value::named("<static-field#" + std::to_string(codeU2(method.code, pc + 1)) + ">"));
                pc += 3;
                break;

            case 0xb8: {
                uint32_t callPc = static_cast<uint32_t>(pc);
                MethodRef ref = resolveMethodRef(cls, codeU2(method.code, pc + 1));
                if (isNativeRuntimePrintInt(ref)) {
                    trace.runtimePrints.push_back(RuntimePrint{label, callPc, frame.pop()});
                } else {
                    std::vector<size_t> widths = argumentSlotWidths(ref.descriptor);
                    std::vector<Value> callArgs(widths.size());
                    for (size_t i = widths.size(); i > 0; --i) {
                        callArgs[i - 1] = frame.pop();
                    }

                    const ClassFile* targetClass = findClass(classes, ref.className);
                    const MethodInfo* targetMethod = targetClass == nullptr ? nullptr : findMethod(*targetClass, ref);
                    if (targetClass != nullptr && targetMethod != nullptr) {
                        std::optional<Value> result = executeMethod(
                            classes, *targetClass, *targetMethod, callArgs, trace, steps, depth + 1);
                        if (result.has_value()) {
                            frame.push(*result);
                        }
                    } else if (returnsValue(ref.descriptor)) {
                        frame.push(Value::named("<call:" + ref.className + "." + ref.name + ref.descriptor + ">"));
                    }
                }
                pc += 3;
                break;
            }

            case 0xb6:
                (void)frame.pop();
                (void)frame.pop();
                pc += 3;
                break;

            case 0xac:
                return frame.pop();

            case 0xb1:
                return std::nullopt;

            default:
                pc += instructionLength(op);
                break;
        }
    }

    return std::nullopt;
}

} // namespace

ExecutionTrace executeStraightLine(const std::vector<ClassFile>& classes, const ClassFile& cls, const MethodInfo& method) {
    ExecutionTrace trace;
    size_t steps = 0;
    (void)executeMethod(classes, cls, method, {}, trace, steps, 0);
    return trace;
}

} // namespace jvmpoc
