#pragma once

#include "../native_methods.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleCanvas(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupCanvasInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleDisplay(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupDisplayStaticLeaf(const std::string& name, const std::string& descriptor);
NativeLeafFn lookupDisplayInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleFont(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupFontStaticLeaf(const std::string& name, const std::string& descriptor);
NativeLeafFn lookupFontInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupGraphicsInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleImage(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
// Per-method leaves for the per-class call-site cache fast path.
// Returns nullptr if (name, descriptor) does not match any registered Image
// static method (caller falls back to slow cascade).
NativeLeafFn lookupImageStaticLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleMidlet(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupMidletInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handlePlayer(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupPlayerInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleNativeRuntime(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupNativeRuntimeStaticLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleString(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupStringInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleSystem(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupSystemStaticLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleRecordStore(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupRecordStoreStaticLeaf(const std::string& name, const std::string& descriptor);
NativeLeafFn lookupRecordStoreInstanceLeaf(const std::string& name, const std::string& descriptor);
NativeCallResult handleThread(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args);
NativeLeafFn lookupThreadStaticLeaf(const std::string& name, const std::string& descriptor);
NativeLeafFn lookupThreadInstanceLeaf(const std::string& name, const std::string& descriptor);

} // namespace jvmpoc::native_methods
