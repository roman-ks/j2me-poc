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

// Lightweight per-image clip state. Stores only what must persist between a
// setClip and the following drawImage call. Canvas is reconstructed on demand
// in graphicsCanvas() — no embedded Canvas means no vector-header overhead in
// the flat array, keeping NativeCallContext small.
struct ImageCanvasEntry {
    uint32_t id = 0;
    uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int clipX = 0, clipY = 0, clipW = 0, clipH = 0;
};

struct NativeCallContext {
    const JvmHost* host = nullptr;
    const std::vector<ClassFile>* classes = nullptr;
    ExecutionTrace& trace;
    std::function<Value(const Value&, const std::string&)> readField;
    std::function<void(const Value&)> startRunnable;
    std::function<void(uint32_t)> sleepThread;
    std::function<void()> requestRepaint;
    bool& gameCanvasFlushCommitted;
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
    // Pointer to the shared main-framebuffer Canvas, which lives in Runtime so that
    // translate/clip state persists across executeMethod() call depths (e.g. when
    // paint() calls a Java sub-method that also invokes Graphics native methods).
    port::Canvas* mainFbCanvas = nullptr;
    // Flat array of lightweight clip-state entries for image-backed Graphics objects.
    // graphicsCanvas() reconstructs a Canvas into scratchCanvas on each call.
    // Linear scan dominates unordered_map for the 1–2 entries games use, with
    // no hash computation, no heap allocation, and minimal struct growth.
    static constexpr int kMaxImageCanvases = 4;
    ImageCanvasEntry imageCanvasEntries[kMaxImageCanvases] {};
    int imageCanvasCount = 0;
    // Scratch canvas written by graphicsCanvas() on each call; also used for the
    // framebuffer fallback path. Callers must not hold the pointer across calls.
    std::optional<port::Canvas> scratchCanvas = std::nullopt;
    // Last source-image lookup cache. Consecutive drawImage calls almost always
    // reuse the same source (e.g. multiple blits from the same sprite sheet),
    // so a single uint32_t compare skips the ctx.images hash lookup on hit.
    // Must be invalidated whenever ctx.images is mutated (unordered_map insert
    // may rehash and dangle the cached pointer).
    uint32_t lastSourceImageId = 0;
    const port::Image* lastSourceImage = nullptr;
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

// Leaf (per-method) handler caching. The interpreter caches the exact function
// for a (className, name, descriptor) triple at the call site, eliminating all
// string compares on the hot path after warm-up. Tables live in each
// native_methods/<class>.cpp and are the single source of truth for that
// class's method dispatch (used by both the leaf cache and the slow cascade).
using NativeMethodFn = NativeCallResult(*)(
    NativeCallContext&, const std::string&, uint32_t,
    const MethodRef&, const std::vector<Value>&);

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;   // exact descriptor; nullptr or "" means match any descriptor
    NativeMethodFn fn;
};

// Returns the leaf-method handler for invokestatic on
// `className.name(descriptor)`, or nullptr if no static binding exists.
NativeMethodFn resolveNativeStaticMethod(
    const std::string& className,
    const std::string& name,
    const std::string& descriptor);

// Same as resolveNativeStaticMethod for invokevirtual. Does NOT cover
// receiver-type fallbacks (image-by-id, string-by-receiver, Canvas subclass) —
// those still require the slow cascade in handleNativeInstanceCall.
NativeMethodFn resolveNativeInstanceMethod(
    const std::string& className,
    const std::string& name,
    const std::string& descriptor);

} // namespace jvmpoc
