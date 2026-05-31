#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

namespace {

NativeCallResult nm_display_getDisplay(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(ctx.displayRef);
}

NativeCallResult nm_display_setCurrent(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];
    Value previousDisplayable = ctx.currentDisplayable;
    ctx.currentDisplayable = args.size() > 1 ? args[1] : Value::named("0");
    ctx.requestRepaint();
    if (ctx.notifyDisplayChanged) {
        ctx.notifyDisplayChanged(previousDisplayable, ctx.currentDisplayable);
    }
    ctx.trace.displaySetCurrents.push_back(DisplaySetCurrent{
        methodLabel,
        pc,
        receiver,
        ctx.currentDisplayable,
    });
    return handledVoid();
}

const NativeMethodEntry kDisplayMethods[] = {
    {"getDisplay", "(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;", &nm_display_getDisplay},
    {"setCurrent", "(Ljavax/microedition/lcdui/Displayable;)V",                              &nm_display_setCurrent},
};

} // namespace

NativeMethodFn resolveDisplayMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kDisplayMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleDisplay(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveDisplayMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
