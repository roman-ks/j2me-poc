#pragma once

#include "value.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace jvmpoc {

struct LocalWrite {
    uint32_t pc = 0;
    uint16_t index = 0;
    Value value;
};

class Frame {
public:
    explicit Frame(size_t maxLocals);

    void setLocal(uint16_t index, Value value);
    Value local(uint16_t index) const;
    void push(Value value);
    Value pop();

private:
    std::vector<Value> locals_;
    std::vector<Value> stack_;
};

} // namespace jvmpoc
