#include "handlers.hpp"

#include "helpers.hpp"

#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_midlet_init(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_midlet_getAppProperty(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    if (ctx.host != nullptr && args.size() >= 2) {
        const std::string key = stringArg(ctx, args, 1);
        const auto it = ctx.host->appProperties.find(key);
        if (it != ctx.host->appProperties.end()) {
            return handledValue(ctx.internString(it->second));
        }
    }
    return handledValue(Value::ofInt(0)); // null
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kMidletInstanceMethods[] = {
    {"<init>",         "()V",                                       &nm_midlet_init},
    {"getAppProperty", "(Ljava/lang/String;)Ljava/lang/String;",    &nm_midlet_getAppProperty},
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

NativeLeafFn lookupMidletInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kMidletInstanceMethods,
                      sizeof(kMidletInstanceMethods) / sizeof(kMidletInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleMidlet(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupMidletInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
