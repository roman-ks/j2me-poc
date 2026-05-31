#include "handlers.hpp"

#include "helpers.hpp"

#include <optional>

namespace jvmpoc::native_methods {
namespace {

NativeCallResult nm_nr_printInt(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
    std::optional<int> parsed = parseIntValue(value);
    if (ctx.trace.recording) ctx.trace.runtimePrints.push_back(RuntimePrint{
        std::string(ctx.callerLabel),
        pc,
        parsed.has_value() ? Value::ofLong(*parsed) : value,
    });
    return handledVoid();
}

NativeCallResult nm_nr_printLong(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    if (ctx.trace.recording) ctx.trace.runtimePrints.push_back(RuntimePrint{
        std::string(ctx.callerLabel),
        pc,
        args.empty() ? Value::named("<missing-arg>") : args[0],
    });
    return handledVoid();
}

NativeCallResult nm_nr_printString(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
    if (ctx.trace.recording) {
        std::optional<uint32_t> id = stringObjectId(ctx, value);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            std::string(ctx.callerLabel),
            pc,
            id.has_value() && strIt != ctx.strings.end()
                ? Value::named(strIt->second)
                : Value::named("<string:" + value.asText() + ">"),
        });
    }
    return handledVoid();
}

NativeCallResult nm_nr_gc(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& /*args*/) {
    ctx.collectGarbage(std::string(ctx.callerLabel) + " pc=" + std::to_string(pc));
    return handledVoid();
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kNativeRuntimeStaticMethods[] = {
    {"printInt",    "(I)V",                  &nm_nr_printInt},
    {"printLong",   "(J)V",                  &nm_nr_printLong},
    {"printString", "(Ljava/lang/String;)V", &nm_nr_printString},
    {"gc",          "()V",                   &nm_nr_gc},
};

NativeLeafFn lookupLeaf(const NativeMethodEntry* table, size_t n,
                        const std::string& name, const std::string& descriptor) {
    for (size_t i = 0; i < n; ++i) {
        if (name == table[i].name && descriptor == table[i].descriptor) {
            return table[i].fn;
        }
    }
    return nullptr;
}

} // namespace

NativeLeafFn lookupNativeRuntimeStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kNativeRuntimeStaticMethods,
                      sizeof(kNativeRuntimeStaticMethods) / sizeof(kNativeRuntimeStaticMethods[0]),
                      name, descriptor);
}

NativeCallResult handleNativeRuntime(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupNativeRuntimeStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
