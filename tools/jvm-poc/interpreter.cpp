#include "interpreter.hpp"

#include "bytecode.hpp"

#include <optional>

namespace jvmpoc {

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
        trace.localWrites.push_back(LocalWrite{pc, index, value});
    };

    size_t pc = 0;
    while (pc < method.code.size()) {
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

            case 0x60: {
                Value rhs = frame.pop();
                Value lhs = frame.pop();
                std::optional<int> left = parseIntValue(lhs);
                std::optional<int> right = parseIntValue(rhs);
                if (left && right) {
                    frame.push(Value::named(std::to_string(*left + *right)));
                } else {
                    frame.push(Value::named("(" + lhs.text + " + " + rhs.text + ")"));
                }
                ++pc;
                break;
            }

            case 0xb2:
                frame.push(Value::named("<static-field#" + std::to_string(codeU2(method.code, pc + 1)) + ">"));
                pc += 3;
                break;

            case 0xb8: {
                uint32_t callPc = static_cast<uint32_t>(pc);
                MethodRef ref = resolveMethodRef(cls, codeU2(method.code, pc + 1));
                if (ref.className == "dev/roman/j2mepoc/NativeRuntime" &&
                    ref.name == "printInt" &&
                    ref.descriptor == "(I)V") {
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
