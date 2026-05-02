#pragma once

#include <optional>
#include <string>

namespace jvmpoc {

struct Value {
    std::string text;

    static Value named(std::string value) {
        return Value{std::move(value)};
    }
};

std::optional<long long> parseLongValue(const Value& value);
std::optional<int> parseIntValue(const Value& value);

} // namespace jvmpoc
