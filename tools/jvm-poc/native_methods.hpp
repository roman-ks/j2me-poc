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
#include <string_view>
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
    // Caller's frame label (e.g. "dev/roman/hello/Foo.bar()V"), set by the
    // interpreter before each native call. string_view points at the
    // interpreter's local — no copy on the hot path. Used by leaves that
    // emit trace records (replaces the methodLabel parameter dropped from
    // NativeLeafFn).
    std::string_view callerLabel = {};
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

// Option N: class-level handler caching. Resolved once at callCache write time
// and stored on ResolvedCallEntry, then called directly to bypass the string
// compares inside handleNativeStaticCall / handleNativeInstanceCall.
using NativeHandler = NativeCallResult(*)(
    NativeCallContext&, const std::string&, uint32_t,
    const MethodRef&, const std::vector<Value>&);

// Returns the leaf handler for invokestatic on `className`, or nullptr when
// the slow cascade is needed (no className-only mapping exists).
NativeHandler resolveNativeStaticHandler(const std::string& className);

// Returns the leaf handler for invokevirtual on `className`. Receiver-type
// OR fallbacks (handleImage on imageId(receiver), handleString on string
// receivers, handleCanvas on Canvas subclasses) are NOT cached — the slow
// path covers them. Cache hits only fire when className matches one of the
// known native classes exactly.
NativeHandler resolveNativeInstanceHandler(const std::string& className);

// Per-call-site leaf resolution. Returns the leaf for exactly one method,
// or nullptr if no leaf is registered (call must fall back to the slow
// cascade in handleNativeXCall). Run once per call site at slow-path time;
// never on the hot path. See docs/per-class-call-site-cache.plan.md.
NativeLeafFn resolveStaticLeaf(const std::string& className,
                               const std::string& methodName,
                               const std::string& descriptor);
NativeLeafFn resolveInstanceLeaf(const std::string& className,
                                 const std::string& methodName,
                                 const std::string& descriptor);

} // namespace jvmpoc
