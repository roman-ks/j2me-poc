#include "../native_methods.hpp"

#include "handlers.hpp"
#include "helpers.hpp"
#include "../method_resolution.hpp"

namespace jvmpoc {

NativeCallResult handleNativeStaticCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (native_methods::nativeRuntimeClassMatches(ref.className)) {
        return native_methods::handleNativeRuntime(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Display") {
        return native_methods::handleDisplay(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Image") {
        return native_methods::handleImage(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/System") {
        return native_methods::handleSystem(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/Thread") {
        return native_methods::handleThread(ctx, methodLabel, pc, ref, args);
    }

    return NativeCallResult{};
}

NativeCallResult handleNativeInstanceCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.className == "javax/microedition/midlet/MIDlet") {
        return native_methods::handleMidlet(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Display") {
        return native_methods::handleDisplay(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Graphics") {
        return native_methods::handleGraphics(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Image" || native_methods::imageId(receiver).has_value()) {
        return native_methods::handleImage(ctx, methodLabel, pc, ref, args);
    }

    const bool receiverIsCanvas =
        ctx.classes != nullptr &&
        isClassOrSubclassOf(*ctx.classes, ctx.receiverClassName, "javax/microedition/lcdui/Canvas");
    if (ref.className == "javax/microedition/lcdui/Canvas" ||
        (receiverIsCanvas && (ref.name == "getWidth" || ref.name == "getHeight"))) {
        return native_methods::handleCanvas(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/String" || native_methods::isStringObject(ctx, receiver)) {
        return native_methods::handleString(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/Thread") {
        return native_methods::handleThread(ctx, methodLabel, pc, ref, args);
    }

    return NativeCallResult{};
}

} // namespace jvmpoc
