#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleThread(
    NativeCallContext& ctx,
    const std::string& /*methodLabel*/,
    uint32_t /*pc*/,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if ((ref.name == "sleep" && ref.descriptor == "(I)V") ||
        (ref.name == "sleep" && ref.descriptor == "(J)V")) {
        std::optional<long long> millis = !args.empty() ? parseLongValue(args[0]) : std::nullopt;
        if (millis.has_value() && *millis > 0 && ctx.host != nullptr) {
            ctx.host->sleepMillis(static_cast<uint32_t>(*millis));
        }
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods