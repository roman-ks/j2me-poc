#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace port {

class Image;
class Canvas {
public:
    Canvas(int width, int height);
    Canvas(int width, int height, std::vector<uint16_t>& externalFramebuffer);

    void setColor(int rgb);
    void setColor(uint8_t r, uint8_t g, uint8_t b);
    void setClip(int x, int y, int w, int h);
    void clear(int rgb = 0x000000);

    void drawPixel(int x, int y);
    void drawLine(int x1, int y1, int x2, int y2);
    void fillRect(int x, int y, int w, int h);
    void drawRect(int x, int y, int w, int h);
    void drawImage(const Image& image, int x, int y, int anchor = 0);
    void drawString(const char* text, int x, int y, int anchor = 0);
    void drawString(const std::string& text, int x, int y, int anchor = 0) { drawString(text.c_str(), x, y, anchor); }

    int width() const { return m_width; }
    int height() const { return m_height; }

    const std::vector<uint16_t>& framebuffer() const;
    std::vector<uint16_t>& framebuffer();

private:
    bool isInsideClip(int x, int y) const;
    int m_width;
    int m_height;
    int m_clipX = 0;
    int m_clipY = 0;
    int m_clipW = 0;
    int m_clipH = 0;
    std::vector<uint16_t>& activeFramebuffer();
    const std::vector<uint16_t>& activeFramebuffer() const;

    uint16_t m_color = 0;
    std::optional<std::reference_wrapper<std::vector<uint16_t>>> m_externalFramebuffer;
    std::vector<uint16_t> m_framebuffer;
};

class Image {
public:
    int width = 0;
    int height = 0;
    std::vector<uint16_t> pixels;
    std::vector<uint8_t> alphaMask;
    bool hasAlphaMask = false;
    std::string sourcePath;

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    static Image createImage(const std::string& path);
    static Image createImage(int width, int height);

    Canvas getGraphics();
};

using Graphics = Canvas;

} // namespace port
