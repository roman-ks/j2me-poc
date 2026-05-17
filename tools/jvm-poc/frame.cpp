#include "frame.hpp"
#include "value.hpp"

namespace jvmpoc {

void Frame::setLocal(uint16_t index, Value value) {
    if (index < maxLocals_) {
        locals_[index] = std::move(value);
    }
}

const Value& Frame::local(uint16_t index) const {
    static const Value kDefault = Value::ofInt(0);
    if (index < maxLocals_ && locals_[index].isInitialized()) {
        return locals_[index];
    }
    return kDefault;
}

void Frame::push(Value value) {
    if (stackTop_ >= slotsEnd_) {
        // Arena exhausted. Dropping silently produces garbage in subsequent
        // pops but won't corrupt memory; the matching frame-allocation check
        // in executeMethod returns a <frame-arena-overflow> sentinel for
        // recoverable signaling.
        return;
    }
    *stackTop_++ = std::move(value);
}

Value Frame::pop() {
    if (stackTop_ <= stackBase_) {
        return Value::named("<stack-underflow>");
    }
    --stackTop_;
    return std::move(*stackTop_);
}

void Frame::clearStack() {
    while (stackTop_ > stackBase_) {
        --stackTop_;
        *stackTop_ = Value{};
    }
}

} // namespace jvmpoc
