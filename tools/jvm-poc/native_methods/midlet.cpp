#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

namespace {

NativeCallResult nm_midlet_init(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_midlet_getAppProperty(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    if (ctx.host != nullptr && args.size() >= 2) {
        const std::string key = stringArg(ctx, args, 1);
        const auto it = ctx.host->appProperties.find(key);
        if (it != ctx.host->appProperties.end()) {
            return handledValue(ctx.internString(it->second));
        }
    }
    return handledValue(Value::ofInt(0));
}

const NativeMethodEntry kMidletMethods[] = {
    {"<init>",          "()V",                                             &nm_midlet_init},
    {"getAppProperty",  "(Ljava/lang/String;)Ljava/lang/String;",          &nm_midlet_getAppProperty},
};

} // namespace

NativeMethodFn resolveMidletMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kMidletMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleMidlet(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveMidletMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
