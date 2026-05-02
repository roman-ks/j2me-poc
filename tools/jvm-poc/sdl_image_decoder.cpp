#include "sdl_image_decoder.hpp"

#include "j2me_port/Canvas.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "j2me_port/stb_image.h"

namespace jvmpoc {
namespace {

uint16_t rgbToRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((static_cast<uint16_t>(r) >> 3u) << 11u) |
                                 ((static_cast<uint16_t>(g) >> 2u) << 5u) |
                                 (static_cast<uint16_t>(b) >> 3u));
}

bool decodeWithStb(const std::vector<uint8_t>& encoded, port::Image& out) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load_from_memory(
        encoded.data(),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &channels,
        4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        return false;
    }

    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width * height));
    out.alphaMask.clear();
    out.hasAlphaMask = false;

    bool sawOpaque = false;
    bool sawTransparent = false;
    const size_t alphaBytesPerRow = (static_cast<size_t>(width) + 7u) >> 3u;
    out.alphaMask.assign(alphaBytesPerRow * static_cast<size_t>(height), 0u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y * width + x);
            const unsigned char* rgba = decoded + pixelIndex * 4u;
            out.pixels[pixelIndex] = rgbToRgb565(rgba[0], rgba[1], rgba[2]);
            if (rgba[3] >= 128) {
                out.alphaMask[static_cast<size_t>(y) * alphaBytesPerRow + (static_cast<size_t>(x) >> 3u)] |=
                    static_cast<uint8_t>(0x80u >> (static_cast<unsigned>(x) & 7u));
                sawOpaque = true;
            } else {
                sawTransparent = true;
            }
        }
    }
    out.hasAlphaMask = sawOpaque && sawTransparent;
    if (!out.hasAlphaMask) {
        out.alphaMask.clear();
    }

    stbi_image_free(decoded);
    return true;
}

} // namespace

void installSdlImageDecoder() {
    port::Image::setDecoder(&decodeWithStb);
}

} // namespace jvmpoc
