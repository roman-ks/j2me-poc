#pragma once

#include "class_file.hpp"
#include "interpreter.hpp"
#include "j2me_port/Canvas.hpp"
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
    const std::vector<ClassFile>* classes = nullptr;
    ExecutionTrace& trace;
    std::map<uint32_t, std::string>& strings;
    std::map<uint32_t, port::Image>& images;
    uint32_t& nextImageId;
    Value& displayRef;
    Value& currentDisplayable;
    std::optional<std::reference_wrapper<std::vector<uint16_t>>> graphicsFramebuffer;
    int& graphicsWidth;
    int& graphicsHeight;
    int& graphicsColorRgb;
    std::string receiverClassName;
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
