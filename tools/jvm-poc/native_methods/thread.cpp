#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

namespace {

NativeCallResult nm_thread_start(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];
    Value target = ctx.readField(receiver, "target");
    if (!target.isNull()) {
        ctx.startRunnable(target);
    }
    return handledVoid();
}

NativeCallResult nm_thread_yield(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_thread_sleep(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    std::optional<long long> millis = !args.empty() ? parseLongValue(args[0]) : std::nullopt;
    if (millis.has_value() && *millis > 0) {
        ctx.sleepThread(static_cast<uint32_t>(*millis));
    }
    return handledVoid();
}

const NativeMethodEntry kThreadMethods[] = {
    {"start", "()V",  &nm_thread_start},
    {"yield", "()V",  &nm_thread_yield},
    {"sleep", "(I)V", &nm_thread_sleep},
    {"sleep", "(J)V", &nm_thread_sleep},
};

} // namespace

NativeMethodFn resolveThreadMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kThreadMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleThread(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveThreadMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
