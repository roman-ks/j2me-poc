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

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
