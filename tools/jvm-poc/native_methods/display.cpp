#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleDisplay(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "getDisplay" &&
        ref.descriptor == "(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;") {
        return handledValue(ctx.displayRef);
    }

    if (ref.name == "setCurrent" && ref.descriptor == "(Ljavax/microedition/lcdui/Displayable;)V") {
        ctx.currentDisplayable = args.size() > 1 ? args[1] : Value::named("0");
        ctx.trace.displaySetCurrents.push_back(DisplaySetCurrent{
            methodLabel,
            pc,
            receiver,
            ctx.currentDisplayable,
        });
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
