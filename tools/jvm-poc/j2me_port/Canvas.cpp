#include "Canvas.hpp"
#include "J2MECompat.hpp"
#include "BitmapFont5x7.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include <core/log.h>


namespace port {

namespace {

Image::Decoder g_imageDecoder = nullptr;

uint16_t rgbToRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((static_cast<uint16_t>(r) >> 3u) << 11u) |
                                 ((static_cast<uint16_t>(g) >> 2u) << 5u) |
                                 (static_cast<uint16_t>(b) >> 3u));
}

bool alphaMaskBitIsSet(const std::vector<uint8_t>& mask, int width, int x, int y) {
    if (mask.empty() || width <= 0 || x < 0 || y < 0 || x >= width) {
        return false;
    }

    const size_t rowBytes = (static_cast<size_t>(width) + 7u) >> 3u;
    const size_t byteIndex = static_cast<size_t>(y) * rowBytes + (static_cast<size_t>(x) >> 3u);
    if (byteIndex >= mask.size()) {
        return false;
    }

    const uint8_t bit = static_cast<uint8_t>(0x80u >> (static_cast<unsigned>(x) & 7u));
    return (mask[byteIndex] & bit) != 0;
}

bool maskHasRealTransparency(const std::vector<uint8_t>& mask, int width, int height) {
    if (mask.empty() || width <= 0 || height <= 0) {
        return false;
    }

    bool sawOpaque = false;
    bool sawTransparent = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (alphaMaskBitIsSet(mask, width, x, y)) {
                sawOpaque = true;
            } else {
                sawTransparent = true;
            }

            if (sawOpaque && sawTransparent) {
                return true;
            }
        }
    }

    return false;
}


} // namespace

Image Image::createImage(const std::string& path) {
    Image image;
    image.sourcePath = path;

    std::vector<uint8_t> encoded;
    if (g_imageDecoder != nullptr && readResourceAll(path, encoded) && !encoded.empty()) {
        if (g_imageDecoder(encoded, image)) {
            image.sourcePath = path;
            return image;
        }
    }

    const std::string binaryPath = path + ".b";
    if (readResourceAll(binaryPath, encoded) == false || encoded.size() < 5) {
        LOGF_W("Failed to load image resource: %s (binary path: %s)", path.c_str(), binaryPath.c_str());
        return image;
    }

    const uint16_t decodedWidth = static_cast<uint16_t>(encoded[0] | (static_cast<uint16_t>(encoded[1]) << 8u));
    const uint16_t decodedHeight = static_cast<uint16_t>(encoded[2] | (static_cast<uint16_t>(encoded[3]) << 8u));
    const bool hasAlpha = encoded[4] != 0;
    if (decodedWidth == 0 || decodedHeight == 0) {
        LOGF_W("Invalid image dimensions in resource: %s (width: %u, height: %u)", path.c_str(), decodedWidth, decodedHeight);
        return image;
    }

    const size_t pixelCount = static_cast<size_t>(decodedWidth) * static_cast<size_t>(decodedHeight);
    const size_t colorBytes = pixelCount * sizeof(uint16_t);
    const size_t alphaBytesPerRow = (static_cast<size_t>(decodedWidth) + 7u) >> 3u;
    const size_t alphaBytes = hasAlpha ? (alphaBytesPerRow * static_cast<size_t>(decodedHeight)) : 0u;
    const size_t expectedSize = 5u + colorBytes + alphaBytes;
    if (encoded.size() != expectedSize) {
        LOGF_W("Unexpected image data size in resource: %s (expected: %zu, actual: %zu)", path.c_str(), expectedSize, encoded.size());
        return image;
    }

    image.width = static_cast<int>(decodedWidth);
    image.height = static_cast<int>(decodedHeight);
    image.pixels.resize(pixelCount);

    size_t offset = 5u;
    for (size_t i = 0; i < pixelCount; ++i) {
        image.pixels[i] = static_cast<uint16_t>(encoded[offset] | (static_cast<uint16_t>(encoded[offset + 1]) << 8u));
        offset += 2u;
    }

    if (hasAlpha && alphaBytes > 0) {
        image.alphaMask.resize(alphaBytes);
        std::memcpy(image.alphaMask.data(), encoded.data() + static_cast<std::ptrdiff_t>(offset), alphaBytes);
        image.hasAlphaMask = maskHasRealTransparency(image.alphaMask, image.width, image.height);
        if (!image.hasAlphaMask) {
            image.alphaMask.clear();
        }
    }

    return image;
}

void Image::setDecoder(Decoder decoder) {
    g_imageDecoder = decoder;
}

Image Image::createImage(int width, int height) {
    Image image;
    image.width = std::max(0, width);
    image.height = std::max(0, height);
    image.pixels.resize(static_cast<size_t>(image.width * image.height), 0u);
    image.alphaMask.clear();
    image.hasAlphaMask = false;
    return image;
}

Canvas Image::getGraphics() {
    const int w = std::max(0, width);
    const int h = std::max(0, height);
    const size_t expected = static_cast<size_t>(w * h);
    if (pixels.size() != expected) {
        pixels.resize(expected, 0u);
    }
    return Canvas(w, h, pixels);
}

Canvas::Canvas(int width, int height)
    : m_width(std::max(0, width)),
      m_height(std::max(0, height)),
      m_clipW(m_width),
      m_clipH(m_height),
      m_framebuffer(static_cast<size_t>(m_width * m_height), 0u) {}

Canvas::Canvas(int width, int height, std::vector<uint16_t>& externalFramebuffer)
    : m_width(std::max(0, width)),
      m_height(std::max(0, height)),
      m_clipW(m_width),
      m_clipH(m_height),
      m_externalFramebuffer(externalFramebuffer) {
    m_externalFramebuffer->get().resize(static_cast<size_t>(m_width * m_height), 0u);
}

std::vector<uint16_t>& Canvas::activeFramebuffer() {
    if (m_externalFramebuffer.has_value()) {
        return m_externalFramebuffer->get();
    }
    return m_framebuffer;
}

const std::vector<uint16_t>& Canvas::activeFramebuffer() const {
    if (m_externalFramebuffer.has_value()) {
        return m_externalFramebuffer->get();
    }
    return m_framebuffer;
}

std::vector<uint16_t>& Canvas::framebuffer() {
    return activeFramebuffer();
}

const std::vector<uint16_t>& Canvas::framebuffer() const {
    return activeFramebuffer();
}

void Canvas::setColor(int rgb) {
    const uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    m_color = rgbToRgb565(r, g, b);
}

void Canvas::setColor(uint8_t r, uint8_t g, uint8_t b) {
    m_color = rgbToRgb565(r, g, b);
}

void Canvas::setClip(int x, int y, int w, int h) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(m_width, x + std::max(0, w));
    const int y1 = std::min(m_height, y + std::max(0, h));

    if (x0 >= x1 || y0 >= y1) {
        m_clipX = 0;
        m_clipY = 0;
        m_clipW = 0;
        m_clipH = 0;
        return;
    }

    m_clipX = x0;
    m_clipY = y0;
    m_clipW = x1 - x0;
    m_clipH = y1 - y0;
}

void Canvas::clear(int rgb) {
    setColor(rgb);
    auto& fb = activeFramebuffer();
    std::fill(fb.begin(), fb.end(), m_color);
}

void Canvas::drawPixel(int x, int y) {
    if (!isInsideClip(x, y)) {
        return;
    }

    auto& fb = activeFramebuffer();
    fb[static_cast<size_t>(y * m_width + x)] = m_color;
}

void Canvas::drawLine(int x1, int y1, int x2, int y2) {
    int dx = std::abs(x2 - x1);
    int sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1);
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        drawPixel(x1, y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }

        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void Canvas::fillRect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }

    int x0 = std::max(m_clipX, x);
    int y0 = std::max(m_clipY, y);
    int x1 = std::min(m_clipX + m_clipW, x + w);
    int y1 = std::min(m_clipY + m_clipH, y + h);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    auto& fb = activeFramebuffer();
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            fb[static_cast<size_t>(py * m_width + px)] = m_color;
        }
    }
}

void Canvas::drawRect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int px = x; px < x + w; ++px) {
        drawPixel(px, y);
        drawPixel(px, y + h - 1);
    }

    for (int py = y; py < y + h; ++py) {
        drawPixel(x, py);
        drawPixel(x + w - 1, py);
    }
}

void Canvas::drawImage(const Image& image, int x, int y, int anchor) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return;
    }

    int drawX = x;
    int drawY = y;
    if ((anchor & 1) != 0) {
        drawX -= image.width / 2;
    } else if ((anchor & 8) != 0) {
        drawX -= image.width;
    }

    if ((anchor & 2) != 0) {
        drawY -= image.height / 2;
    } else if ((anchor & 32) != 0 || (anchor & 64) != 0) {
        drawY -= image.height;
    }

    const int clipX1 = m_clipX + m_clipW;
    const int clipY1 = m_clipY + m_clipH;
    const int srcX0 = std::max(0, m_clipX - drawX);
    const int srcY0 = std::max(0, m_clipY - drawY);
    const int srcX1 = std::min(image.width, clipX1 - drawX);
    const int srcY1 = std::min(image.height, clipY1 - drawY);
    if (srcX0 >= srcX1 || srcY0 >= srcY1) {
        return;
    }

    auto& fb = activeFramebuffer();
    for (int py = srcY0; py < srcY1; ++py) {
        const int dstY = drawY + py;
        const size_t srcRow = static_cast<size_t>(py * image.width);
        const size_t dstRow = static_cast<size_t>(dstY * m_width);

        for (int px = srcX0; px < srcX1; ++px) {
            if (image.hasAlphaMask && !alphaMaskBitIsSet(image.alphaMask, image.width, px, py)) {
                continue;
            }

            const int dstX = drawX + px;
            fb[dstRow + static_cast<size_t>(dstX)] = image.pixels[srcRow + static_cast<size_t>(px)];
        }
    }
}

void Canvas::drawString(const char* text, int x, int y, int anchor) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 8;
    constexpr int kGlyphAdvance = 6;

    const int textLen = static_cast<int>(std::strlen(text));
    int drawX = x;
    int drawY = y;
    const int textWidth = textLen * kGlyphAdvance - 1;
    const int textHeight = kGlyphHeight;

    if ((anchor & 1) != 0) {
        drawX -= textWidth / 2;
    } else if ((anchor & 8) != 0) {
        drawX -= textWidth;
    }

    if ((anchor & 2) != 0) {
        drawY -= textHeight / 2;
    } else if ((anchor & 32) != 0 || (anchor & 64) != 0) {
        drawY -= textHeight;
    }

    auto& fb = activeFramebuffer();
    for (int charIndex = 0; charIndex < textLen; ++charIndex) {
        unsigned char ch = static_cast<unsigned char>(text[static_cast<size_t>(charIndex)]);
        if (ch < 32 || ch > 127) {
            ch = static_cast<unsigned char>('?');
        }

        const size_t glyphOffset = static_cast<size_t>(ch - 32) * kGlyphWidth;
        const int glyphX = drawX + charIndex * kGlyphAdvance;

        for (int col = 0; col < kGlyphWidth; ++col) {
            const uint8_t bits = kBitmapFont5x7[glyphOffset + static_cast<size_t>(col)];
            for (int row = 0; row < kGlyphHeight; ++row) {
                if ((bits & (1u << row)) == 0) {
                    continue;
                }

                const int dstX = glyphX + col;
                const int dstY = drawY + row;
                if (!isInsideClip(dstX, dstY)) {
                    continue;
                }

                fb[static_cast<size_t>(dstY * m_width + dstX)] = m_color;
            }
        }
    }
}

bool Canvas::isInsideClip(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
        return false;
    }

    if (m_clipW <= 0 || m_clipH <= 0) {
        return false;
    }

    return x >= m_clipX && y >= m_clipY && x < m_clipX + m_clipW && y < m_clipY + m_clipH;
}

} // namespace port
