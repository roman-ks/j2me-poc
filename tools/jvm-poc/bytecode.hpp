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

} // namespace jvmpoc
