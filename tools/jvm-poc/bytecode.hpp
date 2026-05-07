#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jvmpoc {

uint8_t codeU1(const std::vector<uint8_t>& code, size_t pc);
int8_t codeS1(const std::vector<uint8_t>& code, size_t pc);
int16_t codeS2(const std::vector<uint8_t>& code, size_t pc);
uint16_t codeU2(const std::vector<uint8_t>& code, size_t pc);
size_t instructionLength(uint8_t op);

// Fast-path overloads operating on a raw pointer (no bounds check).
// Used in the performance-critical bytecode dispatch loop when the code
// array has already been bounds-validated via codeSize.
inline uint8_t codeU1(const uint8_t* code, size_t pc) {
    return code[pc];
}
inline int8_t codeS1(const uint8_t* code, size_t pc) {
    return static_cast<int8_t>(code[pc]);
}
inline int16_t codeS2(const uint8_t* code, size_t pc) {
    return static_cast<int16_t>(
        (static_cast<uint16_t>(code[pc]) << 8) | static_cast<uint16_t>(code[pc + 1]));
}
inline uint16_t codeU2(const uint8_t* code, size_t pc) {
    return (static_cast<uint16_t>(code[pc]) << 8) | static_cast<uint16_t>(code[pc + 1]);
}

} // namespace jvmpoc
