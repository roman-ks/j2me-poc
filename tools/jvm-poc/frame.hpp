#pragma once

#include "value.hpp"

#include <cstdint>
#include <cstddef>
#include <string>

namespace jvmpoc {

struct LocalWrite {
    std::string methodLabel;
    uint32_t pc = 0;
    uint16_t index = 0;
    std::string localName;
    Value value;
    std::string reason;
};

// Non-owning view over slots in Runtime's FrameArena.
// Layout in arena: [ locals_[0..maxLocals)  stackBase_[0..stackTop_) ... ]
//
// stackTop_ is unconstrained by maxStack_ — it's bounded only by the arena
// end. This matches today's interpreter behavior, where Frame::push silently
// extended the operand stack past .class-declared max_stack via vector growth.
// The arena allocates each new frame at the actual stackTop_ of the calling
// frame (not stackBase_+maxStack), so any overshoot is absorbed without
// corrupting the next frame's region.
class Frame {
public:
    Frame() = default;

    Frame(Value* slots, Value* arenaEnd, uint16_t maxLocals, uint16_t maxStack, size_t stackUsed = 0)
        : locals_(slots),
          stackBase_(slots + maxLocals),
          stackTop_(slots + maxLocals + stackUsed),
          slotsEnd_(arenaEnd),
          maxLocals_(maxLocals),
          maxStack_(maxStack) {}

    void setLocal(uint16_t index, Value value);
    const Value& local(uint16_t index) const;
    void push(Value value);
    Value pop();
    void clearStack();

    const Value* localsData() const { return locals_; }
    size_t localsSize() const { return maxLocals_; }
    const Value* stackData() const { return stackBase_; }
    size_t stackSize() const { return static_cast<size_t>(stackTop_ - stackBase_); }
    uint16_t maxStack() const { return maxStack_; }

    // Where the next frame allocated on top of this one starts.
    Value* stackEnd() { return stackTop_; }
    const Value* stackEnd() const { return stackTop_; }

    // Raw slot pointer (locals_), used by arena suspend/resume to copy the
    // [locals... stack...] range out/in.
    Value* slotsBegin() { return locals_; }
    const Value* slotsBegin() const { return locals_; }

private:
    Value* locals_ = nullptr;
    Value* stackBase_ = nullptr;
    Value* stackTop_ = nullptr;
    Value* slotsEnd_ = nullptr;  // arena.end — push bounds check
    uint16_t maxLocals_ = 0;
    uint16_t maxStack_ = 0;
};

} // namespace jvmpoc
