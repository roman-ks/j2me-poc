#pragma once

#include <cstdint>

namespace jvmpoc {

class JvmHost {
public:
    virtual ~JvmHost() = default;

    virtual int screenWidth() const = 0;
    virtual int screenHeight() const = 0;
    virtual uint32_t millis() const = 0;
    virtual void sleepMillis(uint32_t /*ms*/) const {}
    virtual void present(const uint16_t* pixels, int width, int height) = 0;
};

class NullJvmHost final : public JvmHost {
public:
    int screenWidth() const override { return 240; }
    int screenHeight() const override { return 320; }
    uint32_t millis() const override { return 0; }
    void present(const uint16_t* /*pixels*/, int /*width*/, int /*height*/) override {}
};

} // namespace jvmpoc
