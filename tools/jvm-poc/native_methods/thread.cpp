#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_thread_start(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];
    Value target = ctx.readField(receiver, "target");
    if (!target.isNull()) {
        ctx.startRunnable(target);
    }
    return handledVoid();
}

NativeCallResult nm_thread_yield(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_thread_sleep(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    std::optional<long long> millis = !args.empty() ? parseLongValue(args[0]) : std::nullopt;
    if (millis.has_value() && *millis > 0) {
        ctx.sleepThread(static_cast<uint32_t>(*millis));
    }
    return handledVoid();
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kThreadStaticMethods[] = {
    {"yield", "()V",  &nm_thread_yield},
    {"sleep", "(I)V", &nm_thread_sleep},
    {"sleep", "(J)V", &nm_thread_sleep},
};

constexpr NativeMethodEntry kThreadInstanceMethods[] = {
    {"start", "()V", &nm_thread_start},
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

NativeLeafFn lookupThreadStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kThreadStaticMethods,
                      sizeof(kThreadStaticMethods) / sizeof(kThreadStaticMethods[0]),
                      name, descriptor);
}

NativeLeafFn lookupThreadInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kThreadInstanceMethods,
                      sizeof(kThreadInstanceMethods) / sizeof(kThreadInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleThread(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupThreadStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    if (NativeLeafFn leaf = lookupThreadInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
