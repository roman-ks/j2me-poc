#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"
#include "../j2me_port/BitmapFont5x7.hpp"

namespace jvmpoc::native_methods {
namespace {

static const Value kMissingReceiver = Value::named("<missing-receiver>");

// ---------------------------------------------------------------------------
// Leaf method handlers. Same Canvas + GameCanvas methods that used to live as
// inline if-blocks in handleCanvas. Each is now reachable directly via the
// leaf cache (NativeMethodFn) on the hot path.
// ---------------------------------------------------------------------------

// Canvas.<init>()V — no-op shim.
NativeCallResult nm_canvas_init(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_canvas_getWidth(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& ref, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    const int value = ctx.host != nullptr ? ctx.host->screenWidth() : 240;
    if (ctx.trace.recording) ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
        methodLabel, pc, receiver, methodName(ref), value,
    });
    return handledValue(Value::ofInt(static_cast<int32_t>(value)));
}

NativeCallResult nm_canvas_getHeight(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& ref, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    const int value = ctx.host != nullptr ? ctx.host->screenHeight() : 320;
    if (ctx.trace.recording) ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
        methodLabel, pc, receiver, methodName(ref), value,
    });
    return handledValue(Value::ofInt(static_cast<int32_t>(value)));
}

NativeCallResult nm_canvas_setFullScreenMode(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

// Used by both Canvas.repaint()V and Canvas.repaint(IIII)V — body is identical.
NativeCallResult nm_canvas_repaint(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    ctx.requestRepaint();
    return handledVoid();
}

NativeCallResult nm_canvas_serviceRepaints(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    ctx.requestRepaint();
    ctx.sleepThread(0);
    return handledVoid();
}

// GameCanvas.<init>(Z)V — no-op shim.
NativeCallResult nm_gameCanvas_init_z(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_gameCanvas_getGraphics(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(Value::kHandleGfxTag | 0));
}

// Used by both GameCanvas.flushGraphics()V and GameCanvas.flushGraphics(IIII)V.
NativeCallResult nm_gameCanvas_flushGraphics(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    ctx.gameCanvasFlushCommitted = true;
    ctx.requestRepaint();
    return handledVoid();
}

// ---------------------------------------------------------------------------
// Single source of truth: the method table. Same handler can appear under
// multiple descriptors (e.g. repaint has both ()V and (IIII)V forms that
// share the same body — list both so the leaf cache binds either).
// ---------------------------------------------------------------------------
const NativeMethodEntry kCanvasMethods[] = {
    // Canvas
    {"<init>",            "()V",                                           &nm_canvas_init},
    {"getWidth",          "()I",                                           &nm_canvas_getWidth},
    {"getHeight",         "()I",                                           &nm_canvas_getHeight},
    {"setFullScreenMode", "(Z)V",                                          &nm_canvas_setFullScreenMode},
    {"repaint",           "()V",                                           &nm_canvas_repaint},
    {"repaint",           "(IIII)V",                                       &nm_canvas_repaint},
    {"serviceRepaints",   "()V",                                           &nm_canvas_serviceRepaints},
    // GameCanvas
    {"<init>",            "(Z)V",                                          &nm_gameCanvas_init_z},
    {"getGraphics",       "()Ljavax/microedition/lcdui/Graphics;",         &nm_gameCanvas_getGraphics},
    {"flushGraphics",     "()V",                                           &nm_gameCanvas_flushGraphics},
    {"flushGraphics",     "(IIII)V",                                       &nm_gameCanvas_flushGraphics},
};

} // namespace

NativeMethodFn resolveCanvasMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kCanvasMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveCanvasMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
