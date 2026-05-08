#include "frame.hpp"
#include "value.hpp"

namespace jvmpoc {

Frame::Frame(size_t maxLocals, size_t maxStack) : locals_(maxLocals) {
    stack_.reserve(maxStack);
}

void Frame::setLocal(uint16_t index, Value value) {
    if (index < locals_.size()) {
        locals_[index] = std::move(value);
    }
}

const Value& Frame::local(uint16_t index) const {
    // Returns a ref into locals_ — callers that push() this will copy once, not twice.
    // pop() already uses std::move so no copy there.
    static const Value kDefault = Value::ofInt(0);
    if (index < locals_.size() && locals_[index].isInitialized()) {
        return locals_[index];
    }
    return kDefault;
}

void Frame::push(Value value) {
    stack_.push_back(std::move(value));
}

Value Frame::pop() {
    if (stack_.empty()) {
        return Value::named("<stack-underflow>");
    }
    Value value = std::move(stack_.back()); // move out to avoid string copy
    stack_.pop_back();
    return value;
}

} // namespace jvmpoc

