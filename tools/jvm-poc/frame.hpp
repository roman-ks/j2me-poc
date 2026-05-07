#pragma once

#include "sram_allocator.hpp"
#include "value.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace jvmpoc {

struct LocalWrite {
    std::string methodLabel;
    uint32_t pc = 0;
    uint16_t index = 0;
    std::string localName;
    Value value;
    std::string reason;
};

class Frame {
public:
    using ValueVec = std::vector<Value, SramAllocator<Value>>;

    explicit Frame(size_t maxLocals);

    void setLocal(uint16_t index, Value value);
    Value local(uint16_t index) const;
    void push(Value value);
    Value pop();
    const ValueVec& locals() const { return locals_; }
    const ValueVec& stack() const { return stack_; }

private:
    ValueVec locals_;
    ValueVec stack_;
};

} // namespace jvmpoc
