#pragma once

#include <cstdint>
#include <vector>

namespace jvmpoc {

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
