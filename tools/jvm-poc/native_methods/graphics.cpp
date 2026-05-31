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

// ---------------------------------------------------------------------------
// Leaf method handlers. Each function is invoked directly by the interpreter
// (via the cached NativeMethodFn pointer) on the hot path — zero string
// compares between entry and method body. Signature matches NativeMethodFn.
// ---------------------------------------------------------------------------

static const Value kMissingReceiver = Value::named("<missing-receiver>");

NativeCallResult nm_drawImage_iii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
#if JVM_ENABLE_NATIVE_PROFILING
    const uint32_t tHG = (ctx.host && ctx.host->profileNatives) ? nowUs() : 0;
#else
    constexpr uint32_t tHG = 0;
#endif
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    if (tHG) ctx.host->drawImageScanStats.record(nowUs() - tHG);
    // Sub-phase timer: measures everything INSIDE this branch before the actual blit
    // (imageId, images.find, graphicsCanvas, intArg calls).
    // "routing" overhead = native_total - drawImageStats_total - drawImageSetupStats_total.
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
        methodLabel,
        pc,
        "drawImage(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_drawRegion_iiiiiiiii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
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
            intArg(args, 2), intArg(args, 3),  // xSrc, ySrc
            intArg(args, 4), intArg(args, 5),  // width, height
            intArg(args, 6),                    // transform
            intArg(args, 7), intArg(args, 8),  // xDest, yDest
            intArg(args, 9));                   // anchor
    }
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        methodLabel, pc,
        "drawRegion(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + "," + argText(args, 5) + ",...)",
    });
    return handledVoid();
}

NativeCallResult nm_translate_ii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) canvas->translate(intArg(args, 1), intArg(args, 2));
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{methodLabel, pc,
        "translate(" + argText(args, 1) + "," + argText(args, 2) + ")"});
    return handledVoid();
}

NativeCallResult nm_getColor(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(ctx.graphicsColorRgb));
}

NativeCallResult nm_drawRect_iiii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    port::Canvas* canvas = graphicsCanvas(ctx, receiver);
    if (canvas != nullptr) canvas->drawRect(intArg(args, 1), intArg(args, 2), intArg(args, 3), intArg(args, 4));
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{methodLabel, pc,
        "drawRect(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + "," + argText(args, 4) + ")"});
    return handledVoid();
}

NativeCallResult nm_setColor_i(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    ctx.graphicsColorRgb = intArg(args, 1);
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        methodLabel,
        pc,
        "setColor(" + argText(args, 1) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_setColor_iii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    ctx.graphicsColorRgb = (intArg(args, 1) << 16) | (intArg(args, 2) << 8) | intArg(args, 3);
    if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
        methodLabel,
        pc,
        "setColor(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_fillRect_iiii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
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
        methodLabel,
        pc,
        "fillRect(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_setClip_iiii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    int x = intArg(args, 1);
    int y = intArg(args, 2);
    int width = intArg(args, 3);
    int height = intArg(args, 4);
    std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
    if (targetImage.has_value() && *targetImage == 0 && ctx.mainFbCanvas != nullptr) {
        ctx.mainFbCanvas->setClip(x, y, width, height);
    } else if (targetImage.has_value() && *targetImage != 0) {
        // Find or create entry; persist clip fields directly — no Canvas construction.
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
        methodLabel,
        pc,
        "setClip(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_drawLine_iiii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
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
        methodLabel,
        pc,
        "drawLine(" + argText(args, 1) + "," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

NativeCallResult nm_setFont(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    // Only one physical font (port::kBitmapFont5x7). No-op.
    return handledVoid();
}

NativeCallResult nm_getFont(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::named("font:default"));
}

NativeCallResult nm_drawString(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
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
        methodLabel,
        pc,
        "drawString(\"" + text + "\"," + argText(args, 2) + "," +
            argText(args, 3) + "," + argText(args, 4) + ")",
    });
    return handledVoid();
}

// ---------------------------------------------------------------------------
// Single source of truth: the method table. The leaf-cache resolver and the
// class-level slow-path dispatcher both consume this. Adding a new native
// method is one row + one nm_* function — no edits in dispatch.cpp.
// ---------------------------------------------------------------------------
const NativeMethodEntry kGraphicsMethods[] = {
    {"drawImage",  "(Ljavax/microedition/lcdui/Image;III)V",       &nm_drawImage_iii},
    {"drawRegion", "(Ljavax/microedition/lcdui/Image;IIIIIIII)V",  &nm_drawRegion_iiiiiiiii},
    {"translate",  "(II)V",                                         &nm_translate_ii},
    {"getColor",   "()I",                                           &nm_getColor},
    {"drawRect",   "(IIII)V",                                       &nm_drawRect_iiii},
    {"setColor",   "(I)V",                                          &nm_setColor_i},
    {"setColor",   "(III)V",                                        &nm_setColor_iii},
    {"fillRect",   "(IIII)V",                                       &nm_fillRect_iiii},
    {"setClip",    "(IIII)V",                                       &nm_setClip_iiii},
    {"drawLine",   "(IIII)V",                                       &nm_drawLine_iiii},
    {"setFont",    "(Ljavax/microedition/lcdui/Font;)V",            &nm_setFont},
    {"getFont",    "()Ljavax/microedition/lcdui/Font;",             &nm_getFont},
    {"drawString", "(Ljava/lang/String;III)V",                      &nm_drawString},
};

} // namespace

// Public: leaf-method resolver. Linear scan is fine for ≤15 entries; first
// hit wins. Used by both the interpreter cache (fast path) and handleGraphics
// (slow-cascade path).
NativeMethodFn resolveGraphicsMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kGraphicsMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

// Thin slow-path dispatcher. Still invoked through the receiver-type cascade
// in handleNativeInstanceCall when className doesn't statically bind, or when
// the leaf cache was never warmed for this site.
NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveGraphicsMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
