#pragma once

#include "class_file.hpp"
#include "interpreter.hpp"
#include "j2me_port/Canvas.hpp"
#include "value.hpp"

#include <functional>
#include <map>
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>

namespace jvmpoc {

class JvmHost;

struct NativeCallContext {
    const JvmHost* host = nullptr;
    const std::vector<ClassFile>* classes = nullptr;
    ExecutionTrace& trace;
    std::function<Value(const Value&, const std::string&)> readField;
    std::function<void(const Value&)> startRunnable;
    std::function<void(uint32_t)> sleepThread;
    std::function<void()> requestRepaint;
    std::unordered_map<uint32_t, std::string>& strings;
    std::unordered_map<uint32_t, std::vector<Value>>& arrays;
    std::unordered_map<uint32_t, port::Image>& images;
    std::map<std::string, uint32_t>& resourceImages;
    uint32_t& nextImageId;
    Value& displayRef;
    Value& currentDisplayable;
    uint16_t* graphicsFramebuffer;
    int& graphicsWidth;
    int& graphicsHeight;
    int& graphicsColorRgb;
    std::string receiverClassName;
    std::function<void(std::string)> collectGarbage;
    std::function<Value(const std::string&)> internString;
    // Sub-profiling: set tProfT0 = tN just before calling handleNativeInstanceCall,
    // then tProfTEntry is set inside the function body on entry.
    // disp_call = tProfTEntry - tProfT0  (function-call overhead)
    // disp_fn   = nowUs()@handleGraphics_call - tProfTEntry  (className checks in dispatch.cpp)
    uint32_t tProfT0 = 0;
    uint32_t tProfTEntry = 0;
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
