#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

namespace {

// javax.microedition.media.Player — no audio playback in this runtime.
// Calls below are silenced to handledVoid() / handledValue() rather than
// falling into recordUnknownCall so the warn log stays useful for genuinely
// missing APIs. Receivers are typically the unknown-call placeholder Value
// returned by Manager.createPlayer; we never dereference it.

NativeCallResult nm_player_void(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_player_getState(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    // Return UNREALIZED (100) so callers that compare against PREFETCHED (300) /
    // STARTED (400) treat the player as inactive.
    return handledValue(Value::ofInt(100));
}

const NativeMethodEntry kPlayerMethods[] = {
    {"stop",               "()V",                                                &nm_player_void},
    {"deallocate",         "()V",                                                &nm_player_void},
    {"close",              "()V",                                                &nm_player_void},
    {"start",              "()V",                                                &nm_player_void},
    {"realize",            "()V",                                                &nm_player_void},
    {"prefetch",           "()V",                                                &nm_player_void},
    {"setLoopCount",       "(I)V",                                               &nm_player_void},
    {"addPlayerListener",  "(Ljavax/microedition/media/PlayerListener;)V",       &nm_player_void},
    {"getState",           "()I",                                                &nm_player_getState},
};

} // namespace

NativeMethodFn resolvePlayerMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kPlayerMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handlePlayer(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolvePlayerMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
