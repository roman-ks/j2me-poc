#include "handlers.hpp"

#include "helpers.hpp"

#include <algorithm>

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

        if (!id.has_value() || !source.has_value() || offset < 0 || count < 0) {
            return handledVoid();
        }

        auto primIt = ctx.primitiveArrays.find(*source);
        auto arrayIt = ctx.arrays.find(*source);
        const bool hasPrim = (primIt != ctx.primitiveArrays.end());
        const bool hasValues = (arrayIt != ctx.arrays.end());
        if (!hasPrim && !hasValues) {
            return handledVoid();
        }
        const size_t totalSize = hasPrim ? primIt->second.size() : arrayIt->second.size();
        size_t begin = static_cast<size_t>(offset);
        size_t end = begin + static_cast<size_t>(count);
        if (begin > totalSize || end > totalSize) {
            return handledVoid();
        }

        std::string text;
        text.reserve(static_cast<size_t>(count));
        for (size_t index = begin; index < end; ++index) {
            int ch = hasPrim ? static_cast<int>(primIt->second[index])
                             : intArg(arrayIt->second, static_cast<int>(index));
            text.push_back(static_cast<char>(ch & 0xff));
        }
        ctx.strings[*id] = std::move(text);
        return handledVoid();
    }

    if (ref.name == "length" && ref.descriptor == "()I") {
        // strings.find on an object-handle id naturally returns end() for any
        // non-String object (id present in ctx.strings only when string-init
        // populated it), so the explicit "is this a string" validation in
        // stringObjectId would just duplicate the find we're about to do.
        std::optional<uint32_t> id = objectId(receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        return handledValue(strIt != ctx.strings.end()
            ? Value::ofInt(static_cast<int32_t>(strIt->second.size()))
            : Value::named("<string-length:" + receiver.asText() + ">"));
    }

    if (ref.name == "charAt" && ref.descriptor == "(I)C") {
        std::optional<uint32_t> id = objectId(receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        const int index = intArg(args, 1);
        if (strIt != ctx.strings.end() &&
            index >= 0 && static_cast<size_t>(index) < strIt->second.size()) {
            const unsigned char c = static_cast<unsigned char>(strIt->second[static_cast<size_t>(index)]);
            return handledValue(Value::ofInt(static_cast<int32_t>(c)));
        }
        return handledValue(Value::ofInt(0));
    }

    if (ref.name == "indexOf" && ref.descriptor == "(II)I") {
        std::optional<uint32_t> id = objectId(receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        int ch = intArg(args, 1);
        int fromIndex = intArg(args, 2);
        if (strIt == ctx.strings.end() || ch < 0 || ch > 255) {
            return handledValue(Value::ofInt(-1));
        }
        if (fromIndex < 0) {
            fromIndex = 0;
        }
        const std::string& text = strIt->second;
        for (size_t i = static_cast<size_t>(fromIndex); i < text.size(); ++i) {
            if (static_cast<unsigned char>(text[i]) == static_cast<unsigned char>(ch)) {
                return handledValue(Value::ofInt(static_cast<int32_t>(i)));
            }
        }
        return handledValue(Value::ofInt(-1));
    }

    if (ref.name == "equals" && ref.descriptor == "(Ljava/lang/Object;)Z") {
        // Content equality when both sides resolve to strings in ctx.strings;
        // otherwise fall back to raw handle identity. This avoids needing the
        // interpreter's instanceof/checkcast (0xc0/0xc1) opcodes — neither is
        // implemented yet — at the cost of returning false for the
        // non-String-arg case where real Java would also return false. Edge
        // case where 'other' is null: receiver.equals(null) → false.
        std::optional<uint32_t> leftId = objectId(receiver);
        Value rhs = args.size() > 1 ? args[1] : Value::ofInt(0);
        if (rhs.isNull()) {
            return handledValue(Value::ofInt(0));
        }
        std::optional<uint32_t> rightId = objectId(rhs);
        auto leftIt = leftId.has_value() ? ctx.strings.find(*leftId) : ctx.strings.end();
        auto rightIt = rightId.has_value() ? ctx.strings.find(*rightId) : ctx.strings.end();
        if (leftIt != ctx.strings.end() && rightIt != ctx.strings.end()) {
            return handledValue(Value::ofInt(leftIt->second == rightIt->second ? 1 : 0));
        }
        // Handle identity fallback (matches Object.equals default semantics).
        const bool same = leftId.has_value() && rightId.has_value() && *leftId == *rightId;
        return handledValue(Value::ofInt(same ? 1 : 0));
    }

    if (ref.name == "compareTo" && ref.descriptor == "(Ljava/lang/String;)I") {
        std::optional<uint32_t> leftId = objectId(receiver);
        std::optional<uint32_t> rightId = args.size() > 1 ? objectId(args[1]) : std::nullopt;
        auto leftIt = leftId.has_value() ? ctx.strings.find(*leftId) : ctx.strings.end();
        auto rightIt = rightId.has_value() ? ctx.strings.find(*rightId) : ctx.strings.end();
        if (leftIt == ctx.strings.end()) {
            return handledValue(Value::ofInt(rightIt != ctx.strings.end() ? -1 : 0));
        }
        if (rightIt == ctx.strings.end()) {
            return handledValue(Value::ofInt(1));
        }
        const std::string& left = leftIt->second;
        const std::string& right = rightIt->second;
        const size_t count = std::min(left.size(), right.size());
        for (size_t i = 0; i < count; ++i) {
            const int diff = static_cast<int>(static_cast<unsigned char>(left[i])) -
                static_cast<int>(static_cast<unsigned char>(right[i]));
            if (diff != 0) {
                return handledValue(Value::ofInt(diff));
            }
        }
        return handledValue(Value::ofInt(static_cast<int32_t>(left.size()) - static_cast<int32_t>(right.size())));
    }

    if (ref.name == "getChars" && ref.descriptor == "(II[CI)V") {
        std::optional<uint32_t> string = objectId(receiver);
        int srcBegin = intArg(args, 1);
        int srcEnd = intArg(args, 2);
        std::optional<uint32_t> dst = args.size() > 3 ? arrayId(args[3]) : std::nullopt;
        int dstBegin = intArg(args, 4);

        auto strIt = string.has_value() ? ctx.strings.find(*string) : ctx.strings.end();
        if (strIt == ctx.strings.end() || !dst.has_value() ||
            srcBegin < 0 || srcEnd < srcBegin ||
            static_cast<size_t>(srcEnd) > strIt->second.size() || dstBegin < 0) {
            return handledVoid();
        }
        const int copyLen = srcEnd - srcBegin;
        auto primIt = ctx.primitiveArrays.find(*dst);
        if (primIt != ctx.primitiveArrays.end()) {
            if (static_cast<size_t>(dstBegin + copyLen) <= primIt->second.size()) {
                for (int i = 0; i < copyLen; ++i) {
                    unsigned char c = static_cast<unsigned char>(strIt->second[static_cast<size_t>(srcBegin + i)]);
                    primIt->second[static_cast<size_t>(dstBegin + i)] = static_cast<int32_t>(c);
                }
            }
        } else {
            auto arrayIt = ctx.arrays.find(*dst);
            if (arrayIt != ctx.arrays.end() &&
                static_cast<size_t>(dstBegin + copyLen) <= arrayIt->second.size()) {
                for (int i = 0; i < copyLen; ++i) {
                    unsigned char c = static_cast<unsigned char>(strIt->second[static_cast<size_t>(srcBegin + i)]);
                    arrayIt->second[static_cast<size_t>(dstBegin + i)] = Value::ofInt(static_cast<int32_t>(c));
                }
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
