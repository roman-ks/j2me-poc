#include "handlers.hpp"

#include "helpers.hpp"

#include <algorithm>

namespace jvmpoc::native_methods {

NativeCallResult handleStringBuffer(
    NativeCallContext& ctx,
    const std::string& /*methodLabel*/,
    uint32_t /*pc*/,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (ref.name == "makeString" && ref.descriptor == "([CI)Ljava/lang/String;") {
        std::optional<uint32_t> source = args.empty() ? std::nullopt : arrayId(args[0]);
        int count = intArg(args, 1);
        auto arrayIt = source.has_value() ? ctx.arrays.find(*source) : ctx.arrays.end();
        if (!source.has_value() || arrayIt == ctx.arrays.end() || count < 0) {
            return handledValue(ctx.internString(""));
        }

        std::string text;
        size_t len = std::min(static_cast<size_t>(count), arrayIt->second.size());
        text.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            int ch = intArg(arrayIt->second, i);
            text.push_back(static_cast<char>(ch & 0xff));
        }
        return handledValue(ctx.internString(text));
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
