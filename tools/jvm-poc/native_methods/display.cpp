#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_display_getDisplay(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(ctx.displayRef);
}

NativeCallResult nm_display_setCurrent(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];
    Value previousDisplayable = ctx.currentDisplayable;
    ctx.currentDisplayable = args.size() > 1 ? args[1] : Value::named("0");
    ctx.requestRepaint();
    if (ctx.notifyDisplayChanged) {
        ctx.notifyDisplayChanged(previousDisplayable, ctx.currentDisplayable);
    }
    ctx.trace.displaySetCurrents.push_back(DisplaySetCurrent{
        std::string(ctx.callerLabel),
        pc,
        receiver,
        ctx.currentDisplayable,
    });
    return handledVoid();
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kDisplayStaticMethods[] = {
    {"getDisplay", "(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;",
        &nm_display_getDisplay},
};

constexpr NativeMethodEntry kDisplayInstanceMethods[] = {
    {"setCurrent", "(Ljavax/microedition/lcdui/Displayable;)V", &nm_display_setCurrent},
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

NativeLeafFn lookupDisplayStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kDisplayStaticMethods,
                      sizeof(kDisplayStaticMethods) / sizeof(kDisplayStaticMethods[0]),
                      name, descriptor);
}

NativeLeafFn lookupDisplayInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kDisplayInstanceMethods,
                      sizeof(kDisplayInstanceMethods) / sizeof(kDisplayInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleDisplay(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupDisplayStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    if (NativeLeafFn leaf = lookupDisplayInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
