#include "handlers.hpp"

#include "helpers.hpp"

#include <algorithm>

namespace jvmpoc::native_methods {
namespace {

uint16_t rgb565(int red, int green, int blue) {
    red = std::max(0, std::min(255, red));
    green = std::max(0, std::min(255, green));
    blue = std::max(0, std::min(255, blue));
    return static_cast<uint16_t>(((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3));
}

void fillRect(NativeCallContext& ctx, int x, int y, int width, int height) {
    if (ctx.graphicsPixels == nullptr || ctx.graphicsWidth <= 0 || ctx.graphicsHeight <= 0) {
        return;
    }
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(ctx.graphicsWidth, x + width);
    int y1 = std::min(ctx.graphicsHeight, y + height);
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            ctx.graphicsPixels[static_cast<size_t>(yy * ctx.graphicsWidth + xx)] = ctx.graphicsColor;
        }
    }
}

const char* const* glyphRows(char c) {
    static const char* space[] = {".....", ".....", ".....", ".....", ".....", ".....", "....."};
    static const char* bang[] = { "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.." };
    static const char* d[] = { "....#", "....#", ".###.", "#...#", "#...#", "#...#", ".###." };
    static const char* e[] = { ".....", ".....", ".###.", "#...#", "#####", "#....", ".####" };
    static const char* h[] = { "#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#" };
    static const char* l[] = { ".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###." };
    static const char* o[] = { ".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###." };
    static const char* r[] = { ".....", ".....", "#.##.", "##..#", "#....", "#....", "#...." };
    static const char* w[] = { "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#" };
    static const char* fallback[] = { "#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#####" };

    switch (c) {
        case ' ': return space;
        case '!': return bang;
        case 'H': return h;
        case 'W': return w;
        case 'd': return d;
        case 'e': return e;
        case 'l': return l;
        case 'o': return o;
        case 'r': return r;
        default: return fallback;
    }
}

void drawTinyText(NativeCallContext& ctx, const std::string& text, int x, int y, int anchor) {
    constexpr int hcenter = 1;
    constexpr int baseline = 64;
    constexpr int charWidth = 5;
    constexpr int charHeight = 7;
    constexpr int charAdvance = 6;
    int drawX = x;
    int drawY = y;
    int textWidth = text.empty() ? 0 : static_cast<int>(text.size()) * charAdvance - 1;
    if ((anchor & hcenter) != 0) {
        drawX -= textWidth / 2;
    }
    if ((anchor & baseline) != 0) {
        drawY -= charHeight;
    }

    for (size_t i = 0; i < text.size(); ++i) {
        int cx = drawX + static_cast<int>(i) * charAdvance;
        const char* const* rows = glyphRows(text[i]);
        for (int yy = 0; yy < charHeight; ++yy) {
            for (int xx = 0; xx < charWidth; ++xx) {
                if (rows[yy][xx] == '#') {
                    fillRect(ctx, cx + xx, drawY + yy, 1, 1);
                }
            }
        }
    }
}

} // namespace

NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (ref.name == "setColor" && ref.descriptor == "(III)V") {
        ctx.graphicsColor = rgb565(intArg(args, 1), intArg(args, 2), intArg(args, 3));
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "setColor(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + ")",
        });
        return handledVoid();
    }

    if (ref.name == "fillRect" && ref.descriptor == "(IIII)V") {
        int x = intArg(args, 1);
        int y = intArg(args, 2);
        int width = intArg(args, 3);
        int height = intArg(args, 4);
        fillRect(ctx, x, y, width, height);
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "fillRect(" + argText(args, 1) + "," + argText(args, 2) + "," +
                argText(args, 3) + "," + argText(args, 4) + ")",
        });
        return handledVoid();
    }

    if (ref.name == "drawString" && ref.descriptor == "(Ljava/lang/String;III)V") {
        std::string text = stringArg(ctx, args, 1);
        int x = intArg(args, 2);
        int y = intArg(args, 3);
        int anchor = intArg(args, 4);
        drawTinyText(ctx, text, x, y, anchor);
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "drawString(\"" + text + "\"," + argText(args, 2) + "," +
                argText(args, 3) + "," + argText(args, 4) + ")",
        });
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
