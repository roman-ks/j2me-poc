#include "../native_methods.hpp"

#include "handlers.hpp"
#include "helpers.hpp"
#include "../method_resolution.hpp"
#include "../jvm_host.hpp"

#ifdef ESP32_BUILD
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace jvmpoc {

namespace {
inline uint32_t nowUs() {
#ifdef ESP32_BUILD
    return static_cast<uint32_t>(esp_timer_get_time());
#else
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
} // namespace

NativeCallResult handleNativeStaticCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {

    if (ref.className == "javax/microedition/lcdui/Display") {
        return native_methods::handleDisplay(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Image") {
        return native_methods::handleImage(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Font") {
        return native_methods::handleFont(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/System") {
        return native_methods::handleSystem(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/Thread") {
        return native_methods::handleThread(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/rms/RecordStore") {
        return native_methods::handleRecordStore(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "dev/roman/hello/NativeRuntime") {
        return native_methods::handleNativeRuntime(ctx, methodLabel, pc, ref, args);
    }

    return NativeCallResult{};
}

NativeHandler resolveNativeStaticHandler(const std::string& className) {
    if (className == "javax/microedition/lcdui/Display") return &native_methods::handleDisplay;
    if (className == "javax/microedition/lcdui/Image")   return &native_methods::handleImage;
    if (className == "javax/microedition/lcdui/Font")    return &native_methods::handleFont;
    if (className == "java/lang/System")                 return &native_methods::handleSystem;
    if (className == "java/lang/Thread")                 return &native_methods::handleThread;
    if (className == "javax/microedition/rms/RecordStore") return &native_methods::handleRecordStore;
    if (className == "dev/roman/hello/NativeRuntime")    return &native_methods::handleNativeRuntime;
    return nullptr;
}

NativeCallResult handleNativeInstanceCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    // Record function-entry time for disp_call measurement.
#if JVM_ENABLE_NATIVE_PROFILING
    if (ctx.tProfT0) {
        ctx.tProfTEntry = nowUs();
        if (ctx.host) ctx.host->drawImageDispCallStats.record(ctx.tProfTEntry - ctx.tProfT0);
    }
#endif
    // Use a reference to avoid copying the Value (which holds a heap std::string for object refs).
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];

    if (ref.className == "javax/microedition/midlet/MIDlet") {
        return native_methods::handleMidlet(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Display") {
        return native_methods::handleDisplay(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Graphics") {
#if JVM_ENABLE_NATIVE_PROFILING
        if (ctx.tProfTEntry && ctx.host) ctx.host->drawImageDispFnStats.record(nowUs() - ctx.tProfTEntry);
#endif
        return native_methods::handleGraphics(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Image" || native_methods::imageId(receiver).has_value()) {
        return native_methods::handleImage(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/lcdui/Font") {
        return native_methods::handleFont(ctx, methodLabel, pc, ref, args);
    }

    const bool receiverIsCanvas =
        ctx.classes != nullptr &&
        isClassOrSubclassOf(*ctx.classes, ctx.receiverClassName, "javax/microedition/lcdui/Canvas");
    if (ref.className == "javax/microedition/lcdui/Canvas" ||
        ref.className == "javax/microedition/lcdui/game/GameCanvas" ||
        (receiverIsCanvas && (ref.name == "getWidth" || ref.name == "getHeight"))) {
        return native_methods::handleCanvas(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/String" || native_methods::isStringObject(ctx, receiver)) {
        return native_methods::handleString(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "java/lang/Thread") {
        return native_methods::handleThread(ctx, methodLabel, pc, ref, args);
    }

    if (ref.className == "javax/microedition/rms/RecordStore") {
        return native_methods::handleRecordStore(ctx, methodLabel, pc, ref, args);
    }

    // if (ref.className == "javax/microedition/media/Player") {
    //     return native_methods::handlePlayer(ctx, methodLabel, pc, ref, args);
    // }

    return NativeCallResult{};
}

NativeHandler resolveNativeInstanceHandler(const std::string& className) {
    if (className == "javax/microedition/midlet/MIDlet")    return &native_methods::handleMidlet;
    if (className == "javax/microedition/lcdui/Display")    return &native_methods::handleDisplay;
    if (className == "javax/microedition/lcdui/Graphics")   return &native_methods::handleGraphics;
    if (className == "javax/microedition/lcdui/Image")      return &native_methods::handleImage;
    if (className == "javax/microedition/lcdui/Font")       return &native_methods::handleFont;
    if (className == "javax/microedition/lcdui/Canvas")                    return &native_methods::handleCanvas;
    if (className == "javax/microedition/lcdui/game/GameCanvas")           return &native_methods::handleCanvas;
    if (className == "java/lang/String")                    return &native_methods::handleString;
    if (className == "java/lang/Thread")                    return &native_methods::handleThread;
    if (className == "javax/microedition/rms/RecordStore")  return &native_methods::handleRecordStore;
    if (className == "javax/microedition/media/Player")     return &native_methods::handlePlayer;
    return nullptr;
}

} // namespace jvmpoc
