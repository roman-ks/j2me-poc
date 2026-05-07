#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jvmpoc {

struct DrawCallStats {
    uint32_t count = 0;
    uint64_t totalUs = 0;
    uint32_t minUs = 0xFFFFFFFFu;
    uint32_t maxUs = 0;

    void record(uint32_t us) {
        ++count;
        totalUs += us;
        if (us < minUs) minUs = us;
        if (us > maxUs) maxUs = us;
    }
    void reset() {
        count = 0; totalUs = 0; minUs = 0xFFFFFFFFu; maxUs = 0;
    }
    uint32_t avgUs() const { return count > 0 ? static_cast<uint32_t>(totalUs / count) : 0; }
};

enum class HostKeyEventType {
    Press,
    Release,
};

struct HostKeyEvent {
    HostKeyEventType type = HostKeyEventType::Press;
    int keyCode = 0;
};

class JvmHost {
public:
    virtual ~JvmHost() = default;

    virtual int screenWidth() const = 0;
    virtual int screenHeight() const = 0;
    virtual uint32_t millis() const = 0;
    virtual void sleepMillis(uint32_t /*ms*/) const {}
    virtual void present(const uint16_t* pixels, int width, int height) = 0;

    // Per-frame draw stats accumulated by native graphics handlers.
    // Mutable so they can be updated through a const JvmHost* in NativeCallContext.
    mutable DrawCallStats drawImageStats;
    mutable DrawCallStats fillRectStats;
    // Per-frame interpreter op stats (set by dispatch loop via rt.host).
    mutable DrawCallStats getfieldStats;   // 0xb4: heap object field reads
    mutable DrawCallStats arrayLoadStats;  // 0x32/33/34: array element reads
    mutable DrawCallStats putfieldStats;   // 0xb5: heap object field writes
    mutable DrawCallStats localLoadStats;  // 0x1a-0x2d + 0x15/16/19: local var reads (frame.push/local)
    mutable DrawCallStats arithStats;      // 0x60-0x84: arithmetic + iinc (pure stack ops)
    mutable DrawCallStats storeStats;      // 0x36-0x4e: local variable writes
    mutable DrawCallStats arrayStoreStats; // 0x4f-0x56: array element writes
    mutable DrawCallStats branchStats;     // 0x99-0xa7, 0xc6-0xc7: branch instructions
    mutable DrawCallStats invokeStats;     // 0xb6-0xb8: dispatch overhead only (excl. callee body)
    mutable DrawCallStats pushStats;       // 0x01-0x14: const push + ldc
    mutable DrawCallStats miscStats;       // pop/dup/return/getstatic/new/arraylength/default
    mutable uint32_t bytecodeSteps = 0;    // total bytecodes dispatched per frame

    // Per-native-method profiling (only active when profileNatives == true)
    bool profileNatives = false;
    mutable std::unordered_map<std::string, DrawCallStats> nativeStats;

    virtual void handlePress(int keyCode) {
        inputEvents_.push_back(HostKeyEvent{HostKeyEventType::Press, keyCode});
    }
    virtual void handleRelease(int keyCode) {
        inputEvents_.push_back(HostKeyEvent{HostKeyEventType::Release, keyCode});
    }

    std::vector<HostKeyEvent> drainInputEvents() const {
        std::vector<HostKeyEvent> drained = inputEvents_;
        inputEvents_.clear();
        return drained;
    }

protected:
    mutable std::vector<HostKeyEvent> inputEvents_;
};

class NullJvmHost final : public JvmHost {
public:
    int screenWidth() const override { return 240; }
    int screenHeight() const override { return 320; }
    uint32_t millis() const override { return 0; }
    void present(const uint16_t* /*pixels*/, int /*width*/, int /*height*/) override {}
};

} // namespace jvmpoc
