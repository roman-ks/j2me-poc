#include "frame.hpp"
#include "value.hpp"

namespace jvmpoc {

Frame::Frame(size_t maxLocals) : locals_(maxLocals) {}

void Frame::setLocal(uint16_t index, Value value) {
    if (index < locals_.size()) {
        locals_[index] = std::move(value);
    }
}

Value Frame::local(uint16_t index) const {
    if (index < locals_.size() && locals_[index].isInitialized()) {
        return locals_[index];
    }
    return Value::named("local" + std::to_string(index));
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

