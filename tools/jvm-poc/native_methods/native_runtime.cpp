#include "handlers.hpp"

#include "helpers.hpp"

#include <optional>

namespace jvmpoc::native_methods {

namespace {

NativeCallResult nm_runtime_printInt(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
    std::optional<int> parsed = parseIntValue(value);
    if (ctx.trace.recording) ctx.trace.runtimePrints.push_back(RuntimePrint{
        methodLabel,
        pc,
        parsed.has_value() ? Value::ofLong(*parsed) : value,
    });
    return handledVoid();
}

NativeCallResult nm_runtime_printLong(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    if (ctx.trace.recording) ctx.trace.runtimePrints.push_back(RuntimePrint{
        methodLabel,
        pc,
        args.empty() ? Value::named("<missing-arg>") : args[0],
    });
    return handledVoid();
}

NativeCallResult nm_runtime_printString(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
    if (ctx.trace.recording) {
        std::optional<uint32_t> id = stringObjectId(ctx, value);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            methodLabel,
            pc,
            id.has_value() && strIt != ctx.strings.end()
                ? Value::named(strIt->second)
                : Value::named("<string:" + value.asText() + ">"),
        });
    }
    return handledVoid();
}

NativeCallResult nm_runtime_gc(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    ctx.collectGarbage(methodLabel + " pc=" + std::to_string(pc));
    return handledVoid();
}

const NativeMethodEntry kNativeRuntimeMethods[] = {
    {"printInt",    "(I)V",                     &nm_runtime_printInt},
    {"printLong",   "(J)V",                     &nm_runtime_printLong},
    {"printString", "(Ljava/lang/String;)V",    &nm_runtime_printString},
    {"gc",          "()V",                      &nm_runtime_gc},
};

} // namespace

NativeMethodFn resolveNativeRuntimeMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kNativeRuntimeMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleNativeRuntime(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveNativeRuntimeMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
