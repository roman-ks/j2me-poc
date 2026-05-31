#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"
#include "../j2me_port/BitmapFont5x7.hpp"

namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_canvas_init(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_canvas_getWidth(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    const int value = ctx.host != nullptr ? ctx.host->screenWidth() : 240;
    if (ctx.trace.recording) ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
        std::string(ctx.callerLabel),
        pc,
        receiver,
        "getWidth()I",
        value,
    });
    return handledValue(Value::ofInt(static_cast<int32_t>(value)));
}

NativeCallResult nm_canvas_getHeight(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    const int value = ctx.host != nullptr ? ctx.host->screenHeight() : 320;
    if (ctx.trace.recording) ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
        std::string(ctx.callerLabel),
        pc,
        receiver,
        "getHeight()I",
        value,
    });
    return handledValue(Value::ofInt(static_cast<int32_t>(value)));
}

NativeCallResult nm_canvas_setFullScreenMode(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_canvas_repaint(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    ctx.requestRepaint();
    return handledVoid();
}

NativeCallResult nm_canvas_serviceRepaints(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    ctx.requestRepaint();
    ctx.sleepThread(0);
    return handledVoid();
}

NativeCallResult nm_canvas_getGraphics(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(Value::kHandleGfxTag | 0));
}

NativeCallResult nm_canvas_flushGraphics(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    ctx.gameCanvasFlushCommitted = true;
    ctx.requestRepaint();
    return handledVoid();
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kCanvasInstanceMethods[] = {
    {"<init>",            "()V",                                          &nm_canvas_init},
    {"<init>",            "(Z)V",                                         &nm_canvas_init},
    {"getWidth",          "()I",                                          &nm_canvas_getWidth},
    {"getHeight",         "()I",                                          &nm_canvas_getHeight},
    {"setFullScreenMode", "(Z)V",                                         &nm_canvas_setFullScreenMode},
    {"repaint",           "()V",                                          &nm_canvas_repaint},
    {"repaint",           "(IIII)V",                                      &nm_canvas_repaint},
    {"serviceRepaints",   "()V",                                          &nm_canvas_serviceRepaints},
    {"getGraphics",       "()Ljavax/microedition/lcdui/Graphics;",        &nm_canvas_getGraphics},
    {"flushGraphics",     "()V",                                          &nm_canvas_flushGraphics},
    {"flushGraphics",     "(IIII)V",                                      &nm_canvas_flushGraphics},
};

NativeLeafFn lookupLeaf(const NativeMethodEntry* table, size_t n,
                        const std::string& name, const std::string& descriptor) {
    for (size_t i = 0; i < n; ++i) {
        if (name == table[i].name && descriptor == table[i].descriptor) {
            return table[i].fn;
        }
    }
    return nullptr;
}

} // namespace

NativeLeafFn lookupCanvasInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kCanvasInstanceMethods,
                      sizeof(kCanvasInstanceMethods) / sizeof(kCanvasInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupCanvasInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
