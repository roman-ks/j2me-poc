#include "handlers.hpp"

#include "helpers.hpp"

#include "../j2me_port/Canvas.hpp"
#include "../jvm_host.hpp"

#ifdef ESP32_BUILD
#include <esp_timer.h>
#else
#include <chrono>
#endif
#include <string>

namespace jvmpoc::native_methods {
namespace {

inline uint32_t nowUs() {
#ifdef ESP32_BUILD
    return static_cast<uint32_t>(esp_timer_get_time());
#else
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

port::Canvas* graphicsCanvas(NativeCallContext& ctx, const Value& receiver) {
    std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
    if (targetImage.has_value()) {
        if (*targetImage != 0) {
            // Scan flat array for cached entry (warm path: 1 comparison).
            for (int i = 0; i < ctx.imageCanvasCount; ++i) {
                if (ctx.imageCanvasEntries[i].id == *targetImage) {
                    const ImageCanvasEntry& e = ctx.imageCanvasEntries[i];
                    ctx.scratchCanvas.emplace(e.width, e.height, e.pixels);
                    ctx.scratchCanvas->setClip(e.clipX, e.clipY, e.clipW, e.clipH);
                    ctx.scratchCanvas->setColor(ctx.graphicsColorRgb);
                    return &ctx.scratchCanvas.value();
                }
            }
            // Cache miss — populate entry from the image map.
            auto imageIt = ctx.images.find(*targetImage);
            if (imageIt == ctx.images.end()) {
                return nullptr;
            }
            if (ctx.imageCanvasCount >= NativeCallContext::kMaxImageCanvases) {
                return nullptr;
            }
            ImageCanvasEntry& e = ctx.imageCanvasEntries[ctx.imageCanvasCount++];
            e.id = *targetImage;
            e.pixels = imageIt->second.pixels.data();
            e.width = imageIt->second.getWidth();
            e.height = imageIt->second.getHeight();
            e.clipX = 0; e.clipY = 0; e.clipW = e.width; e.clipH = e.height;
            ctx.scratchCanvas.emplace(e.width, e.height, e.pixels);
            // Full canvas by default — no setClip needed.
            ctx.scratchCanvas->setColor(ctx.graphicsColorRgb);
            return &ctx.scratchCanvas.value();
        }
        // targetImage == 0: main framebuffer sentinel (kHandleGfxTag | 0) — use cached canvas.
        if (ctx.mainFbCanvas != nullptr) {
            ctx.mainFbCanvas->setColor(ctx.graphicsColorRgb);
            return ctx.mainFbCanvas;
        }
        // Fallback if canvas wasn't pre-built (e.g. no framebuffer at ctx creation time).
    }

    if (ctx.graphicsFramebuffer == nullptr || ctx.graphicsWidth <= 0 || ctx.graphicsHeight <= 0) {
        return nullptr;
    }
    ctx.scratchCanvas.emplace(ctx.graphicsWidth, ctx.graphicsHeight, ctx.graphicsFramebuffer);
    ctx.scratchCanvas->setColor(ctx.graphicsColorRgb);
    return &ctx.scratchCanvas.value();
}

NativeCallResult nm_graphics_drawImage_IIII(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
#if JVM_ENABLE_NATIVE_PROFILING
    const uint32_t tHG = (ctx.host && ctx.host->profileNatives) ? nowUs() : 0;
#else
    constexpr uint32_t tHG = 0;
#endif
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    if (tHG) ctx.host->drawImageScanStats.record(nowUs() - tHG);
#if JVM_ENABLE_NATIVE_PROFILING
    const uint32_t tSetup = (ctx.host && ctx.host->profileNatives) ? nowUs() : 0;
#else
    constexpr uint32_t tSetup = 0;
#endif
    std::optional<uint32_t> image = args.size() > 1 ? imageId(args[1]) : std::optional<uint32_t>{};
    const port::Image* imagePtr = nullptr;
    if (image.has_value()) {
        if (*image == ctx.lastSourceImageId && ctx.lastSourceImage != nullptr) {
            imagePtr = ctx.lastSourceImage;
        } else {
            auto imageIt = ctx.images.find(*image);
            if (imageIt != ctx.images.end()) {
                imagePtr = &imageIt->second;
                ctx.lastSourceImageId = *image;
                ctx.lastSourceImage = imagePtr;
            }
        }
    }
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    const int diX = intArg(args, 2);
    const int diY = intArg(args, 3);
    const int diAnchor = intArg(args, 4);
    if (canvas != nullptr && imagePtr != nullptr) {
#if JVM_ENABLE_NATIVE_PROFILING
        const uint32_t t0 = nowUs();
        if (tSetup) ctx.host->drawImageSetupStats.record(t0 - tSetup);
        canvas->drawImage(*imagePtr, diX, diY, diAnchor);
        if (ctx.host != nullptr && ctx.host->profileNatives) ctx.host->drawImageStats.record(nowUs() - t0);
#else
        canvas->drawImage(*imagePtr, diX, diY, diAnchor);
#endif
    } else if (tSetup) {
        ctx.host->drawImageSetupStats.record(nowUs() - tSetup);
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel),
        pc,
        "drawImage(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_drawRegion(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    std::optional<uint32_t> image = args.size() > 1 ? imageId(args[1]) : std::optional<uint32_t>{};
    const port::Image* imagePtr = nullptr;
    if (image.has_value()) {
        auto imageIt = ctx.images.find(*image);
        if (imageIt != ctx.images.end()) imagePtr = &imageIt->second;
    }
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr && imagePtr != nullptr) {
        canvas->drawRegion(*imagePtr,
            intArg(args, 2), intArg(args, 3),
            intArg(args, 4), intArg(args, 5),
            intArg(args, 6),
            intArg(args, 7), intArg(args, 8),
            intArg(args, 9));
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "drawRegion(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + "," + argText(args, 5) + ",...)",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_translate(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) canvas->translate(intArg(args, 1), intArg(args, 2));
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "translate(" + argText(args, 1) + "," + argText(args, 2) + ")"});
    return handledVoid();
}

NativeCallResult nm_graphics_getColor(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(ctx.graphicsColorRgb));
}

NativeCallResult nm_graphics_drawRect(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) canvas->drawRect(intArg(args, 1), intArg(args, 2), intArg(args, 3), intArg(args, 4));
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "drawRect(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + "," + argText(args, 4) + ")"});
    return handledVoid();
}

NativeCallResult nm_graphics_setColor_I(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    ctx.graphicsColorRgb = intArg(args, 1);
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "setColor(" + argText(args, 1) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_setColor_III(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    ctx.graphicsColorRgb = (intArg(args, 1) << 16) | (intArg(args, 2) << 8) | intArg(args, 3);
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "setColor(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_fillRect(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    int x = intArg(args, 1);
    int y = intArg(args, 2);
    int width = intArg(args, 3);
    int height = intArg(args, 4);
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) {
#if JVM_ENABLE_NATIVE_PROFILING
        const uint32_t t0 = nowUs();
        canvas->fillRect(x, y, width, height);
        if (ctx.host != nullptr && ctx.host->profileNatives) ctx.host->fillRectStats.record(nowUs() - t0);
#else
        canvas->fillRect(x, y, width, height);
#endif
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "fillRect(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_setClip(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    int x = intArg(args, 1);
    int y = intArg(args, 2);
    int width = intArg(args, 3);
    int height = intArg(args, 4);
    std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
    if (targetImage.has_value() && *targetImage == 0 && ctx.mainFbCanvas != nullptr) {
        ctx.mainFbCanvas->setClip(x, y, width, height);
    } else if (targetImage.has_value() && *targetImage != 0) {
        ImageCanvasEntry* entry = nullptr;
        for (int i = 0; i < ctx.imageCanvasCount; ++i) {
            if (ctx.imageCanvasEntries[i].id == *targetImage) {
                entry = &ctx.imageCanvasEntries[i];
                break;
            }
        }
        if (entry == nullptr) {
            auto imageIt = ctx.images.find(*targetImage);
            if (imageIt != ctx.images.end() &&
                    ctx.imageCanvasCount < NativeCallContext::kMaxImageCanvases) {
                ImageCanvasEntry& e = ctx.imageCanvasEntries[ctx.imageCanvasCount++];
                e.id = *targetImage;
                e.pixels = imageIt->second.pixels.data();
                e.width = imageIt->second.getWidth();
                e.height = imageIt->second.getHeight();
                e.clipX = 0; e.clipY = 0; e.clipW = e.width; e.clipH = e.height;
                entry = &e;
            }
        }
        if (entry != nullptr) {
            entry->clipX = x; entry->clipY = y;
            entry->clipW = width; entry->clipH = height;
        }
    } else {
        port::Canvas* canvas = graphicsCanvas(ctx, receiver);
        if (canvas != nullptr) {
            canvas->setClip(x, y, width, height);
        }
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "setClip(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_drawLine(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    int x1 = intArg(args, 1);
    int y1 = intArg(args, 2);
    int x2 = intArg(args, 3);
    int y2 = intArg(args, 4);
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) {
        canvas->drawLine(x1, y1, x2, y2);
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "drawLine(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_graphics_setFont(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledVoid();
}

NativeCallResult nm_graphics_getFont(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::named("font:default"));
}

NativeCallResult nm_graphics_drawString(
    NativeCallContext& ctx, uint32_t pc, const std::vector<Value>& args) {
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    std::string text = stringArg(ctx, args, 1);
    int x = intArg(args, 2);
    int y = intArg(args, 3);
    int anchor = intArg(args, 4);
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) {
        canvas->drawString(text, x, y, anchor);
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        std::string(ctx.callerLabel), pc,
        "drawString(\"" + text + "\"," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kGraphicsInstanceMethods[] = {
    {"drawImage",  "(Ljavax/microedition/lcdui/Image;III)V",                 &nm_graphics_drawImage_IIII},
    {"drawRegion", "(Ljavax/microedition/lcdui/Image;IIIIIIII)V",            &nm_graphics_drawRegion},
    {"translate",  "(II)V",                                                   &nm_graphics_translate},
    {"getColor",   "()I",                                                     &nm_graphics_getColor},
    {"drawRect",   "(IIII)V",                                                 &nm_graphics_drawRect},
    {"setColor",   "(I)V",                                                    &nm_graphics_setColor_I},
    {"setColor",   "(III)V",                                                  &nm_graphics_setColor_III},
    {"fillRect",   "(IIII)V",                                                 &nm_graphics_fillRect},
    {"setClip",    "(IIII)V",                                                 &nm_graphics_setClip},
    {"drawLine",   "(IIII)V",                                                 &nm_graphics_drawLine},
    {"setFont",    "(Ljavax/microedition/lcdui/Font;)V",                      &nm_graphics_setFont},
    {"getFont",    "()Ljavax/microedition/lcdui/Font;",                       &nm_graphics_getFont},
    {"drawString", "(Ljava/lang/String;III)V",                                &nm_graphics_drawString},
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

NativeLeafFn lookupGraphicsInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kGraphicsInstanceMethods,
                      sizeof(kGraphicsInstanceMethods) / sizeof(kGraphicsInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupGraphicsInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
