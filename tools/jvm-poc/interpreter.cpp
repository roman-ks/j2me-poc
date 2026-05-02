#include "interpreter.hpp"

#include "bytecode.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace jvmpoc {
namespace {

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

constexpr size_t kMaxSteps = 10000;

} // namespace

ExecutionTrace executeStraightLine(const ClassFile& cls, const MethodInfo& method) {
    ExecutionTrace trace;
    Frame frame(method.maxLocals);

    for (size_t i = 0; i < argumentSlots(method) && i < method.maxLocals; ++i) {
        frame.setLocal(static_cast<uint16_t>(i),
                       Value::named("<arg:" + localNameAt(method, static_cast<uint16_t>(i), 0) + ">"));
    }

    auto store = [&](uint16_t index, uint32_t pc) {
        Value value = frame.pop();
        frame.setLocal(index, value);
        trace.localWrites.push_back(LocalWrite{pc, index, value, "store"});
    };

    size_t pc = 0;
    size_t steps = 0;
    while (pc < method.code.size()) {
        if (++steps > kMaxSteps) {
            trace.stepLimitHit = true;
            return trace;
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
                trace.localWrites.push_back(LocalWrite{iincPc, index, newValue, "iinc"});
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
                    trace.runtimePrints.push_back(RuntimePrint{callPc, frame.pop()});
                }
                pc += 3;
                break;
            }

            case 0xb6:
                (void)frame.pop();
                (void)frame.pop();
                pc += 3;
                break;

            case 0xb1:
                return trace;

            default:
                pc += instructionLength(op);
                break;
        }
    }

    return trace;
}

} // namespace jvmpoc
