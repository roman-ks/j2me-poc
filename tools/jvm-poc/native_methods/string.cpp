#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleString(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];

    if (ref.name == "init" && ref.descriptor == "([CII)V") {
        std::optional<uint32_t> id = objectId(receiver);
        std::optional<uint32_t> source = args.size() > 1 ? arrayId(args[1]) : std::nullopt;
        int offset = intArg(args, 2);
        int count = intArg(args, 3);

        auto arrayIt = source.has_value() ? ctx.arrays.find(*source) : ctx.arrays.end();
        if (!id.has_value() || !source.has_value() || arrayIt == ctx.arrays.end() || offset < 0 || count < 0) {
            return handledVoid();
        }

        const std::vector<Value>& values = arrayIt->second;
        size_t begin = static_cast<size_t>(offset);
        size_t end = begin + static_cast<size_t>(count);
        if (begin > values.size() || end > values.size()) {
            return handledVoid();
        }

        std::string text;
        text.reserve(static_cast<size_t>(count));
        for (size_t index = begin; index < end; ++index) {
            int ch = intArg(values, index);
            text.push_back(static_cast<char>(ch & 0xff));
        }
        ctx.strings[*id] = std::move(text);
        return handledVoid();
    }

    if (ref.name == "length" && ref.descriptor == "()I") {
        std::optional<uint32_t> id = stringObjectId(ctx, receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        return handledValue(id.has_value() && strIt != ctx.strings.end()
            ? Value::named(std::to_string(strIt->second.size()))
            : Value::named("<string-length:" + receiver.asText() + ">"));
    }

    if (ref.name == "getChars" && ref.descriptor == "(II[CI)V") {
        std::optional<uint32_t> string = stringObjectId(ctx, receiver);
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

    if (ctx.trace.recording) ctx.trace.unsupportedStringCalls.push_back(UnsupportedStringCall{
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
