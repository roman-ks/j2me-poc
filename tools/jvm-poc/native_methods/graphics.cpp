#include "handlers.hpp"

#include "helpers.hpp"

#include "../j2me_port/Canvas.hpp"

namespace jvmpoc::native_methods {
namespace {

std::optional<port::Canvas> graphicsCanvas(NativeCallContext& ctx, const Value& receiver) {
    std::optional<uint32_t> targetImage = imageGraphicsId(receiver);
    if (targetImage.has_value()) {
        auto imageIt = ctx.images.find(*targetImage);
        if (imageIt == ctx.images.end()) {
            return std::nullopt;
        }
        port::Canvas canvas(imageIt->second.getWidth(), imageIt->second.getHeight(), imageIt->second.pixels.data());
        canvas.setColor(ctx.graphicsColorRgb);
        return canvas;
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
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "setColor" && ref.descriptor == "(I)V") {
        ctx.graphicsColorRgb = intArg(args, 1);
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "setColor(" + argText(args, 1) + ")",
        });
        return handledVoid();
    }

    if (ref.name == "setColor" && ref.descriptor == "(III)V") {
        ctx.graphicsColorRgb = (intArg(args, 1) << 16) | (intArg(args, 2) << 8) | intArg(args, 3);
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
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value()) {
            canvas->fillRect(x, y, width, height);
        }
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
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value()) {
            canvas->drawString(text, x, y, anchor);
        }
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "drawString(\"" + text + "\"," + argText(args, 2) + "," +
                argText(args, 3) + "," + argText(args, 4) + ")",
        });
        return handledVoid();
    }

    if (ref.name == "drawImage" && ref.descriptor == "(Ljavax/microedition/lcdui/Image;III)V") {
        std::optional<uint32_t> image = imageId(args.size() > 1 ? args[1] : Value::named(""));
        auto imageIt = image.has_value() ? ctx.images.find(*image) : ctx.images.end();
        std::optional<port::Canvas> canvas = graphicsCanvas(ctx, receiver);
        if (canvas.has_value() && image.has_value() && imageIt != ctx.images.end()) {
            canvas->drawImage(imageIt->second, intArg(args, 2), intArg(args, 3), intArg(args, 4));
        }
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "drawImage(" + argText(args, 1) + "," + argText(args, 2) + "," +
                argText(args, 3) + "," + argText(args, 4) + ")",
        });
        return handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
