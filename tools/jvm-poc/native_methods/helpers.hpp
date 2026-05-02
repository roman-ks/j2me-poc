#pragma once

#include "../native_methods.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jvmpoc::native_methods {

bool nativeRuntimeClassMatches(const std::string& className);
bool returnsValue(const std::string& descriptor);
std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix);
std::optional<uint32_t> stringId(const Value& value);
std::optional<uint32_t> arrayId(const Value& value);
std::optional<uint32_t> imageId(const Value& value);
std::optional<uint32_t> imageGraphicsId(const Value& value);
std::string methodName(const MethodRef& ref);
NativeCallResult handledVoid();
NativeCallResult handledValue(Value value);
int intArg(const std::vector<Value>& args, size_t index, int fallback = 0);
std::string argText(const std::vector<Value>& args, size_t index);
std::string stringArg(const NativeCallContext& ctx, const std::vector<Value>& args, size_t index);

} // namespace jvmpoc::native_methods
