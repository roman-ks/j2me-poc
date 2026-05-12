#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleMidlet(
    NativeCallContext& /*ctx*/,
    const std::string& /*methodLabel*/,
    uint32_t /*pc*/,
    const MethodRef& ref,
    const std::vector<Value>& /*args*/) {
    if (ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if (ref.name == "getAppProperty" && ref.descriptor == "(Ljava/lang/String;)Ljava/lang/String;") {
        return handledValue(Value::named("0"));
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
