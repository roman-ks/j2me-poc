#pragma once

#include "../native_methods.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleDisplay(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleFont(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleImage(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleMidlet(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handlePlayer(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleNativeRuntime(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleString(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleSystem(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleRecordStore(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeCallResult handleThread(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);

// Per-class leaf-method resolvers. Each native_methods/<class>.cpp that has
// been converted to the method-table pattern exposes one of these. Returns
// the leaf NativeMethodFn for an exact (name, descriptor) match, or nullptr
// when no static binding exists for that method.
NativeMethodFn resolveGraphicsMethod(const std::string& name, const std::string& descriptor);
NativeMethodFn resolveCanvasMethod(const std::string& name, const std::string& descriptor);
NativeMethodFn resolveImageMethod(const std::string& name, const std::string& descriptor);
NativeMethodFn resolveFontMethod(const std::string& name, const std::string& descriptor);
NativeMethodFn resolveDisplayMethod(const std::string& name, const std::string& descriptor);
NativeMethodFn resolveStringMethod(const std::string& name, const std::string& descriptor);

} // namespace jvmpoc::native_methods
