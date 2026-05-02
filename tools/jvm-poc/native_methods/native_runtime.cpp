#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleNativeRuntime(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (ref.name == "printInt" && ref.descriptor == "(I)V") {
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            methodLabel,
            pc,
            args.empty() ? Value::named("<missing-arg>") : args[0],
        });
        return handledVoid();
    }

    if (ref.name == "printString" && ref.descriptor == "(Ljava/lang/String;)V") {
        Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
        std::optional<uint32_t> id = stringObjectId(ctx, value);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            methodLabel,
            pc,
            id.has_value() && strIt != ctx.strings.end()
                ? Value::named(strIt->second)
                : Value::named("<string:" + value.text + ">"),
        });
        return handledVoid();
    }

    if (ref.name == "gc" && ref.descriptor == "()V") {
        ctx.collectGarbage(methodLabel + " pc=" + std::to_string(pc));
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
