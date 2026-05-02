#include "frame.hpp"

namespace jvmpoc {

Frame::Frame(size_t maxLocals) : locals_(maxLocals) {}

void Frame::setLocal(uint16_t index, Value value) {
    if (index < locals_.size()) {
        locals_[index] = std::move(value);
    }
}

Value Frame::local(uint16_t index) const {
    if (index < locals_.size() && !locals_[index].text.empty()) {
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
    Value value = stack_.back();
    stack_.pop_back();
    return value;
}

std::optional<int> parseIntValue(const Value& value) {
    if (value.text.empty()) {
        return std::nullopt;
    }
    size_t pos = 0;
    if (value.text[0] == '-') {
        pos = 1;
    }
    if (pos == value.text.size()) {
        return std::nullopt;
    }
    for (; pos < value.text.size(); ++pos) {
        if (value.text[pos] < '0' || value.text[pos] > '9') {
            return std::nullopt;
        }
    }
    return std::stoi(value.text);
}

} // namespace jvmpoc
