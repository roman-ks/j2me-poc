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

    if (ref.name == "getChars" && ref.descriptor == "(II[CI)V") {
        std::optional<uint32_t> string = stringId(receiver);
        int srcBegin = intArg(args, 1);
        int srcEnd = intArg(args, 2);
        std::optional<uint32_t> dst = args.size() > 3 ? arrayId(args[3]) : std::nullopt;
        int dstBegin = intArg(args, 4);

        auto strIt = string.has_value() ? ctx.strings.find(*string) : ctx.strings.end();
        auto arrayIt = dst.has_value() ? ctx.arrays.find(*dst) : ctx.arrays.end();
        if (string.has_value() && strIt != ctx.strings.end() &&
            dst.has_value() && arrayIt != ctx.arrays.end() &&
            srcBegin >= 0 && srcEnd >= srcBegin &&
            static_cast<size_t>(srcEnd) <= strIt->second.size() &&
            dstBegin >= 0 &&
            static_cast<size_t>(dstBegin + (srcEnd - srcBegin)) <= arrayIt->second.size()) {
            for (int i = srcBegin; i < srcEnd; ++i) {
                unsigned char c = static_cast<unsigned char>(strIt->second[static_cast<size_t>(i)]);
                arrayIt->second[static_cast<size_t>(dstBegin + i - srcBegin)] = Value::named(std::to_string(static_cast<int>(c)));
            }
        }
        return handledVoid();
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
