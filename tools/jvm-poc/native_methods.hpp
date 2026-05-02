#pragma once

#include "class_file.hpp"
#include "interpreter.hpp"
#include "value.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace jvmpoc {

class JvmHost;

struct NativeCallContext {
    const JvmHost* host = nullptr;
    ExecutionTrace& trace;
    std::map<uint32_t, std::string>& strings;
    Value& displayRef;
    Value& currentDisplayable;
    uint16_t*& graphicsPixels;
    int& graphicsWidth;
    int& graphicsHeight;
    uint16_t& graphicsColor;
    std::function<void(std::string)> collectGarbage;
};

struct NativeCallResult {
    bool handled = false;
    std::optional<Value> returnValue;
};

NativeCallResult handleNativeStaticCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);

NativeCallResult handleNativeInstanceCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);

} // namespace jvmpoc
