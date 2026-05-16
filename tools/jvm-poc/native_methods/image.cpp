#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {
namespace {

Value storeImage(NativeCallContext& ctx, port::Image image) {
    uint32_t id = ctx.nextImageId++;
    ctx.images[id] = std::move(image);
    return Value::ofInt(Value::kHandleImgTag | static_cast<int32_t>(id & 0xFFFFFF));
}

} // namespace

NativeCallResult handleImage(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.name == "createImage" && ref.descriptor == "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;") {
        std::string path = stringArg(ctx, args, 0);
        auto cached = ctx.resourceImages.find(path);
        if (cached != ctx.resourceImages.end()) {
            auto imageIt = ctx.images.find(cached->second);
            if (imageIt != ctx.images.end()) {
                Value imageRef = Value::ofInt(Value::kHandleImgTag | static_cast<int32_t>(cached->second & 0xFFFFFF));
                ctx.trace.imageLoads.push_back(ImageLoad{
                    methodLabel,
                    pc,
                    imageRef,
                    path,
                    imageIt->second.width,
                    imageIt->second.height,
                });
                return handledValue(imageRef);
            }
            ctx.resourceImages.erase(cached);
        }

        port::Image image = port::Image::createImage(path);
        Value imageRef = storeImage(ctx, image);
        std::optional<uint32_t> imageIdValue = imageId(imageRef);
        if (imageIdValue.has_value()) {
            ctx.resourceImages[path] = *imageIdValue;
        }
        ctx.trace.imageLoads.push_back(ImageLoad{
            methodLabel,
            pc,
            imageRef,
            path,
            image.width,
            image.height,
        });
        return handledValue(imageRef);
    }

    if (ref.name == "createImage" && ref.descriptor == "(II)Ljavax/microedition/lcdui/Image;") {
        int width = intArg(args, 0);
        int height = intArg(args, 1);
        port::Image image = port::Image::createImage(width, height);
        Value imageRef = storeImage(ctx, image);
        ctx.trace.imageLoads.push_back(ImageLoad{
            methodLabel,
            pc,
            imageRef,
            "<generated>",
            image.width,
            image.height,
        });
        return handledValue(imageRef);
    }

    std::optional<uint32_t> id = imageId(receiver);
    auto imageIt = id.has_value() ? ctx.images.find(*id) : ctx.images.end();
    if (ref.name == "getWidth" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(
            id.has_value() && imageIt != ctx.images.end() ? imageIt->second.getWidth() : 0));
    }

    if (ref.name == "getHeight" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(
            id.has_value() && imageIt != ctx.images.end() ? imageIt->second.getHeight() : 0));
    }

    if (ref.name == "getGraphics" && ref.descriptor == "()Ljavax/microedition/lcdui/Graphics;") {
        return id.has_value()
            ? handledValue(Value::ofInt(Value::kHandleGfxTag | static_cast<int32_t>(*id & 0xFFFFFF)))
            : handledValue(Value::named("graphics:<missing-image>"));
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
