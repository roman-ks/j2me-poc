#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if ((ref.name == "getWidth" || ref.name == "getHeight") && ref.descriptor == "()I") {
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
        return handledValue(Value::named(std::to_string(value)));
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
