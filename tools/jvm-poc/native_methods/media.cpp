// #include "handlers.hpp"

// #include "helpers.hpp"

// namespace jvmpoc::native_methods {

// // javax.microedition.media.Player — no audio playback in this runtime.
// // Calls below are silenced to handledVoid() / handledValue() rather than
// // falling into recordUnknownCall so the warn log stays useful for genuinely
// // missing APIs. Receivers are typically the unknown-call placeholder Value
// // returned by Manager.createPlayer; we never dereference it.
// NativeCallResult handlePlayer(
//     NativeCallContext& /*ctx*/,
//     const std::string& /*methodLabel*/,
//     uint32_t /*pc*/,
//     const MethodRef& ref,
//     const std::vector<Value>& /*args*/) {

//     if (ref.descriptor == "()V" &&
//         (ref.name == "stop" ||
//          ref.name == "deallocate" ||
//          ref.name == "close" ||
//          ref.name == "start" ||
//          ref.name == "realize" ||
//          ref.name == "prefetch")) {
//         return handledVoid();
//     }

//     if (ref.name == "setLoopCount" && ref.descriptor == "(I)V") {
//         return handledVoid();
//     }
//     if (ref.name == "addPlayerListener" &&
//         ref.descriptor == "(Ljavax/microedition/media/PlayerListener;)V") {
//         return handledVoid();
//     }

//     // Player.getState() — return UNREALIZED (100) so callers that compare
//     // against PREFETCHED (300) / STARTED (400) treat the player as inactive.
//     if (ref.name == "getState" && ref.descriptor == "()I") {
//         return handledValue(Value::ofInt(100));
//     }

//     return NativeCallResult{};
// }

// } // namespace jvmpoc::native_methods
