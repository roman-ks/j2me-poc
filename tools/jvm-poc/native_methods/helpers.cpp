#include "helpers.hpp"

#include <cstdlib>

namespace jvmpoc::native_methods {

bool nativeRuntimeClassMatches(const std::string& className) {
    const std::string suffix = "/NativeRuntime";
    return className == "NativeRuntime" ||
           (className.size() >= suffix.size() &&
            className.compare(className.size() - suffix.size(), suffix.size(), suffix) == 0);
}

bool returnsValue(const std::string& descriptor) {
    size_t close = descriptor.find(')');
    return close != std::string::npos && close + 1 < descriptor.size() && descriptor[close + 1] != 'V';
}

std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix) {
    if (value.text.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.text.c_str() + prefix.size(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> stringId(const Value& value) {
    return parseHandle(value, "str#");
}

std::optional<uint32_t> arrayId(const Value& value) {
    return parseHandle(value, "arr#");
}

std::optional<uint32_t> imageId(const Value& value) {
    return parseHandle(value, "image#");
}

std::optional<uint32_t> imageGraphicsId(const Value& value) {
    return parseHandle(value, "graphics:image#");
}

std::string methodName(const MethodRef& ref) {
    return ref.className + "." + ref.name + ref.descriptor;
}

NativeCallResult handledVoid() {
    return NativeCallResult{true, std::nullopt};
}

NativeCallResult handledValue(Value value) {
    return NativeCallResult{true, std::move(value)};
}

int intArg(const std::vector<Value>& args, size_t index, int fallback) {
    if (index >= args.size()) {
        return fallback;
    }
    std::optional<int> parsed = parseIntValue(args[index]);
    return parsed.has_value() ? *parsed : fallback;
}

std::string argText(const std::vector<Value>& args, size_t index) {
    return index < args.size() ? args[index].text : "<missing-arg>";
}

std::string stringArg(const NativeCallContext& ctx, const std::vector<Value>& args, size_t index) {
    if (index >= args.size()) {
        return "";
    }
    std::optional<uint32_t> id = stringId(args[index]);
    auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
    return id.has_value() && strIt != ctx.strings.end() ? strIt->second : args[index].text;
}

} // namespace jvmpoc::native_methods
