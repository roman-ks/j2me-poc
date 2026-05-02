#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleString(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "length" && ref.descriptor == "()I") {
        std::optional<uint32_t> id = stringId(receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        return handledValue(id.has_value() && strIt != ctx.strings.end()
            ? Value::named(std::to_string(strIt->second.size()))
            : Value::named("<string-length:" + receiver.text + ">"));
    }

    ctx.trace.unsupportedStringCalls.push_back(UnsupportedStringCall{
        methodLabel,
        pc,
        methodName(ref),
        receiver,
    });
    return returnsValue(ref.descriptor)
        ? handledValue(Value::named("<unsupported-string-call:" + ref.name + ">"))
        : handledVoid();
}

} // namespace jvmpoc::native_methods
