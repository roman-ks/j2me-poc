#include "bytecode.hpp"

#include <stdexcept>

namespace jvmpoc {

uint8_t codeU1(const std::vector<uint8_t>& code, size_t pc) {
    if (pc >= code.size()) {
        throw std::runtime_error("bytecode read past end");
    }
    return code[pc];
}

int8_t codeS1(const std::vector<uint8_t>& code, size_t pc) {
    return static_cast<int8_t>(codeU1(code, pc));
}

int16_t codeS2(const std::vector<uint8_t>& code, size_t pc) {
    uint16_t hi = codeU1(code, pc);
    uint16_t lo = codeU1(code, pc + 1);
    return static_cast<int16_t>((hi << 8) | lo);
}

int32_t codeS4(const std::vector<uint8_t>& code, size_t pc) {
    uint32_t b0 = codeU1(code, pc);
    uint32_t b1 = codeU1(code, pc + 1);
    uint32_t b2 = codeU1(code, pc + 2);
    uint32_t b3 = codeU1(code, pc + 3);
    return static_cast<int32_t>((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
}

uint16_t codeU2(const std::vector<uint8_t>& code, size_t pc) {
    uint16_t hi = codeU1(code, pc);
    uint16_t lo = codeU1(code, pc + 1);
    return static_cast<uint16_t>((hi << 8) | lo);
}

size_t instructionLength(uint8_t op) {
    switch (op) {
        case 0x10: return 2;
        case 0x11: return 3;
        case 0x12: return 2;
        case 0x13: return 3;
        case 0x14: return 3;
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: return 2;
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a: return 2;
        case 0x84: return 3;
        case 0x99: case 0x9a: case 0x9b: case 0x9c: case 0x9d: case 0x9e:
        case 0x9f: case 0xa0: case 0xa1: case 0xa2: case 0xa3: case 0xa4:
        case 0xa5: case 0xa6: case 0xa7: case 0xa8:
            return 3;
        case 0xb2: case 0xb3: case 0xb4: case 0xb5:
        case 0xb6: case 0xb7: case 0xb8: case 0xbb: case 0xbd: case 0xc0: case 0xc1:
            return 3;
        case 0xb9: case 0xba:
            return 5;
        case 0xbc:
            return 2;
        case 0xc5:
            return 4;
        case 0xc6: case 0xc7:
            return 3;
        default:
            return 1;
    }
}

} // namespace jvmpoc
