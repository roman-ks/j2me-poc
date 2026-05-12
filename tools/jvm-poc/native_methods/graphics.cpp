#include "handlers.hpp"

#include "helpers.hpp"

#include "../j2me_port/Canvas.hpp"
#include "../jvm_host.hpp"

#ifdef ESP32_BUILD
#include <esp_timer.h>
#else
#include <chrono>
#endif

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

std::optional<port::Canvas> graphicsCanvas(NativeCallContext& ctx, const Value& receiver) {
    std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
    if (targetImage.has_value()) {
        if (*targetImage != 0) {
            auto imageIt = ctx.images.find(*targetImage);
            if (imageIt == ctx.images.end()) {
                return std::nullopt;
            }
            port::Canvas canvas(imageIt->second.getWidth(), imageIt->second.getHeight(), imageIt->second.pixels.data());
            canvas.setColor(ctx.graphicsColorRgb);
            return canvas;
        }
        // targetImage == 0: main framebuffer sentinel (kHandleGfxTag | 0) — use cached canvas.
        if (ctx.mainFbCanvas.has_value()) {
            ctx.mainFbCanvas->setColor(ctx.graphicsColorRgb);
            return *ctx.mainFbCanvas;
        }
        // Fallback if canvas wasn't pre-built (e.g. no framebuffer at ctx creation time).
    }

    if (ctx.graphicsFramebuffer == nullptr || ctx.graphicsWidth <= 0 || ctx.graphicsHeight <= 0) {
        return std::nullopt;
    }
    port::Canvas canvas(ctx.graphicsWidth, ctx.graphicsHeight, ctx.graphicsFramebuffer);
    canvas.setColor(ctx.graphicsColorRgb);
    return canvas;
}

} // namespace

NativeCallResult handleGraphics(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
#if JVM_ENABLE_NATIVE_PROFILING
    const uint32_t tHG = (ctx.host && ctx.host->profileNatives) ? nowUs() : 0;
#else
    constexpr uint32_t tHG = 0;
#endif
    static const Value kMissingReceiver = Value::named("<missing-receiver>");
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];

    if (ref.name == "drawImage" && ref.descriptor == "(Ljavax/microedition/lcdui/Image;III)V") {
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
        auto imageIt = image.has_value() ? ctx.images.find(*image) : ctx.images.end();
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        const int diX = intArg(args, 2);
        const int diY = intArg(args, 3);
        const int diAnchor = intArg(args, 4);
        if (canvas.has_value() && image.has_value() && imageIt != ctx.images.end()) {
#if JVM_ENABLE_NATIVE_PROFILING
            const uint32_t t0 = nowUs();
            if (tSetup) ctx.host->drawImageSetupStats.record(t0 - tSetup);
            canvas->drawImage(imageIt->second, diX, diY, diAnchor);
            if (ctx.host != nullptr && ctx.host->profileNatives) ctx.host->drawImageStats.record(nowUs() - t0);
#else
            canvas->drawImage(imageIt->second, diX, diY, diAnchor);
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

    if (ref.name == "setColor" && ref.descriptor == "(I)V") {
        ctx.graphicsColorRgb = intArg(args, 1);
        if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "setColor(" + argText(args, 1) + ")",
        });
        return handledVoid();
    }

    if (ref.name == "setColor" && ref.descriptor == "(III)V") {
        ctx.graphicsColorRgb = (intArg(args, 1) << 16) | (intArg(args, 2) << 8) | intArg(args, 3);
        if (ctx.trace.recording) ctx.trace.graphicsOps.push_back(GraphicsOp{
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
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value()) {
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

    if (ref.name == "setClip" && ref.descriptor == "(IIII)V") {
        int x = intArg(args, 1);
        int y = intArg(args, 2);
        int width = intArg(args, 3);
        int height = intArg(args, 4);
        std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
        if (targetImage.has_value() && *targetImage == 0 && ctx.mainFbCanvas.has_value()) {
            ctx.mainFbCanvas->setClip(x, y, width, height);
        } else {
            std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
            if (canvas.has_value()) {
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

    if (ref.name == "drawLine" && ref.descriptor == "(IIII)V") {
        int x1 = intArg(args, 1);
        int y1 = intArg(args, 2);
        int x2 = intArg(args, 3);
        int y2 = intArg(args, 4);
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value()) {
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

    if (ref.name == "drawString" && ref.descriptor == "(Ljava/lang/String;III)V") {
        std::string text = stringArg(ctx, args, 1);
        int x = intArg(args, 2);
        int y = intArg(args, 3);
        int anchor = intArg(args, 4);
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value()) {
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

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
