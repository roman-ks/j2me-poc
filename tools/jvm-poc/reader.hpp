#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace jvmpoc {

class Reader {
public:
    explicit Reader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    uint8_t u1() {
        require(1);
        return bytes_[pos_++];
    }

    uint16_t u2() {
        uint16_t hi = u1();
        uint16_t lo = u1();
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    uint32_t u4() {
        uint32_t b1 = u1();
        uint32_t b2 = u1();
        uint32_t b3 = u1();
        uint32_t b4 = u1();
        return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
    }

    void skip(size_t len) {
        require(len);
        pos_ += len;
    }

    std::vector<uint8_t> bytes(size_t len) {
        require(len);
        std::vector<uint8_t> out(bytes_.begin() + static_cast<long>(pos_),
                                 bytes_.begin() + static_cast<long>(pos_ + len));
        pos_ += len;
        return out;
    }

private:
    void require(size_t len) const {
        if (pos_ + len > bytes_.size()) {
            throw std::runtime_error("unexpected end of class file");
        }
    }

    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
};

} // namespace jvmpoc
