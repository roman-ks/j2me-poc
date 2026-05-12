#include "handlers.hpp"

#include "helpers.hpp"

#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleMidlet(
    NativeCallContext& ctx,
    const std::string& /*methodLabel*/,
    uint32_t /*pc*/,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if (ref.name == "getAppProperty" && ref.descriptor == "(Ljava/lang/String;)Ljava/lang/String;") {
        if (ctx.host != nullptr && args.size() >= 2) {
            const std::string key = stringArg(ctx, args, 1);
            const auto it = ctx.host->appProperties.find(key);
            if (it != ctx.host->appProperties.end()) {
                return handledValue(ctx.internString(it->second));
            }
        }
        return handledValue(Value::ofInt(0)); // null
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
