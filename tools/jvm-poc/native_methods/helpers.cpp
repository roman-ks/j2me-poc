#include "helpers.hpp"

#include <cstdlib>

namespace jvmpoc::native_methods {

bool returnsValue(const std::string& descriptor) {
    size_t close = descriptor.find(')');
    return close != std::string::npos && close + 1 < descriptor.size() && descriptor[close + 1] != 'V';
}

std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix) {
    if (value.tag != Value::Tag::kStr) return std::nullopt;
    const std::string& s = *value.str;
    if (s.compare(0, prefix.size(), prefix) != 0) return std::nullopt;
    char* end = nullptr;
    unsigned long parsed = std::strtoul(s.c_str() + prefix.size(), &end, 10);
    if (end == nullptr || *end != '\0') return std::nullopt;
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> objectId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleObjTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

std::optional<uint32_t> stringObjectId(const NativeCallContext& ctx, const Value& value) {
    std::optional<uint32_t> id = objectId(value);
    if (!id.has_value()) return std::nullopt;
    return ctx.strings.find(*id) != ctx.strings.end() ? id : std::nullopt;
}

bool isStringObject(const NativeCallContext& ctx, const Value& value) {
    return stringObjectId(ctx, value).has_value();
}

std::optional<uint32_t> arrayId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleArrTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

std::optional<uint32_t> imageId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleImgTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

std::optional<uint32_t> imageGraphicsId(const Value& value) {
    if (value.tag != Value::Tag::kInt) return std::nullopt;
    if ((value.i32 & Value::kHandleTagMask) == Value::kHandleGfxTag)
        return static_cast<uint32_t>(value.i32 & Value::kHandleIdMask);
    return std::nullopt;
}

std::string methodName(const MethodRefView& ref) {
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
    return index < args.size() ? args[index].asText() : "<missing-arg>";
}

std::string stringArg(const NativeCallContext& ctx, const std::vector<Value>& args, size_t index) {
    if (index >= args.size()) {
        return "";
    }
    std::optional<uint32_t> id = objectId(args[index]);
    auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
    return strIt != ctx.strings.end() ? strIt->second : args[index].asText();
}

} // namespace jvmpoc::native_methods
