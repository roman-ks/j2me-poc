#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"
#include "../j2me_port/BitmapFont5x7.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];

    if (ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if ((ref.name == "getWidth" || ref.name == "getHeight") && ref.descriptor == "()I") {
        // A kStr-tagged receiver is a non-heap sentinel (e.g. font:default mistyped
        // as Canvas by a decompiler). Return font metrics rather than screen dims.
        if (!args.empty() && args[0].tag == Value::Tag::kStr) {
            const int value = ref.name == "getHeight"
                ? port::kBitmapFontHeight
                : port::kBitmapFontAdvance;
            return handledValue(Value::ofInt(static_cast<int32_t>(value)));
        }
        const int value = ref.name == "getWidth"
            ? (ctx.host != nullptr ? ctx.host->screenWidth() : 240)
            : (ctx.host != nullptr ? ctx.host->screenHeight() : 320);
        if (ctx.trace.recording) ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
            methodLabel,
            pc,
            receiver,
            methodName(ref),
            value,
        });
        return handledValue(Value::ofInt(static_cast<int32_t>(value)));
    }

    if (ref.name == "setFullScreenMode" && ref.descriptor == "(Z)V") {
        return handledVoid();
    }

    if ((ref.name == "repaint" && ref.descriptor == "()V") ||
        (ref.name == "repaint" && ref.descriptor == "(IIII)V")) {
        ctx.requestRepaint();
        return handledVoid();
    }

    if (ref.name == "serviceRepaints" && ref.descriptor == "()V") {
        ctx.requestRepaint();
        ctx.sleepThread(0);
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
