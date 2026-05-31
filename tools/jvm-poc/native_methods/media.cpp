#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {
namespace {

// javax.microedition.media.Player — no audio playback in this runtime.
// Calls are silenced to handledVoid() / handledValue() rather than falling
// into recordUnknownCall so the warn log stays useful for genuinely missing
// APIs.

NativeCallResult nm_player_noop(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

// Player.getState() — return UNREALIZED (100) so callers comparing against
// PREFETCHED (300) / STARTED (400) treat the player as inactive.
NativeCallResult nm_player_getState(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(100));
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kPlayerInstanceMethods[] = {
    {"stop",              "()V",                                                  &nm_player_noop},
    {"deallocate",        "()V",                                                  &nm_player_noop},
    {"close",             "()V",                                                  &nm_player_noop},
    {"start",             "()V",                                                  &nm_player_noop},
    {"realize",           "()V",                                                  &nm_player_noop},
    {"prefetch",          "()V",                                                  &nm_player_noop},
    {"setLoopCount",      "(I)V",                                                 &nm_player_noop},
    {"addPlayerListener", "(Ljavax/microedition/media/PlayerListener;)V",        &nm_player_noop},
    {"getState",          "()I",                                                  &nm_player_getState},
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

NativeLeafFn lookupPlayerInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kPlayerInstanceMethods,
                      sizeof(kPlayerInstanceMethods) / sizeof(kPlayerInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handlePlayer(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupPlayerInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
