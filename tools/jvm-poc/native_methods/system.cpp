#include "handlers.hpp"

#include "helpers.hpp"
#include "../jvm_host.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleSystem(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if (ref.name == "gc" && ref.descriptor == "()V") {
        // Handle garbage collection

        // todo - figure out how to use this since all gc methods are in interpreter.cpp 
        // if (ref.className == "java/lang/System" && ref.name == "gc" && ref.descriptor == "()V") {
        //     collectGarbage(rt, callName(ref));
        //     return NativeCallResult{true, std::nullopt};
        // }

        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
