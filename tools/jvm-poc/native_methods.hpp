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
#include <utility>
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
    std::function<void(const Value&, const Value&)> notifyDisplayChanged;
    std::unordered_map<uint32_t, std::string>& strings;
    std::unordered_map<uint32_t, std::vector<Value>>& arrays;
    std::unordered_map<uint32_t, std::vector<int32_t>>& primitiveArrays;
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
    // Cached main-framebuffer Canvas — built once per executeMethod call,
    // reused by graphicsCanvas() for all ID=0 graphics targets.
    std::optional<port::Canvas> mainFbCanvas = std::nullopt;
    // Cached canvases for image-backed Graphics objects (ID != 0),
    // keyed by image ID. Preserves clip and color state across native calls
    // within the same executeMethod invocation.
    std::unordered_map<uint32_t, port::Canvas> imageCanvases;
    // Fallback canvas for the rare path where there is no mainFbCanvas and no
    // image-backed target. graphicsCanvas() writes here and returns a pointer
    // into it so call sites never receive an owned copy.
    std::optional<port::Canvas> scratchCanvas;
};

struct NativeCallResult {
    bool handled = false;
    std::optional<Value> returnValue;
    std::optional<Value> exception;

    NativeCallResult() = default;
    NativeCallResult(
        bool handled,
        std::optional<Value> returnValue = std::nullopt,
        std::optional<Value> exception = std::nullopt)
        : handled(handled),
          returnValue(std::move(returnValue)),
          exception(std::move(exception)) {}
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
