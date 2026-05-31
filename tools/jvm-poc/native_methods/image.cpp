#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {
namespace {

Value storeImage(NativeCallContext& ctx, port::Image image) {
    uint32_t id = ctx.nextImageId++;
    ctx.images[id] = std::move(image);
    ctx.lastSourceImage = nullptr;
    ctx.lastSourceImageId = 0;
    return Value::ofInt(Value::kHandleImgTag | static_cast<int32_t>(id & 0xFFFFFF));
}

NativeCallResult nm_image_createImage_String(
    NativeCallContext& ctx,
    uint32_t pc,
    const std::vector<Value>& args) {
    std::string path = stringArg(ctx, args, 0);
    auto cached = ctx.resourceImages.find(path);
    if (cached != ctx.resourceImages.end()) {
        auto imageIt = ctx.images.find(cached->second);
        if (imageIt != ctx.images.end()) {
            Value imageRef = Value::ofInt(Value::kHandleImgTag | static_cast<int32_t>(cached->second & 0xFFFFFF));
            ctx.trace.imageLoads.push_back(ImageLoad{
                std::string(ctx.callerLabel),
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
        std::string(ctx.callerLabel),
        pc,
        imageRef,
        path,
        image.width,
        image.height,
    });
    return handledValue(imageRef);
}

NativeCallResult nm_image_createImage_BII(
    NativeCallContext& ctx,
    uint32_t pc,
    const std::vector<Value>& args) {
    std::optional<uint32_t> arrId = arrayId(args[0]);
    int offset = intArg(args, 1);
    int length = intArg(args, 2);
    if (arrId.has_value()) {
        auto primIt = ctx.primitiveArrays.find(*arrId);
        if (primIt != ctx.primitiveArrays.end()) {
            const auto& raw = primIt->second;
            if (offset >= 0 && length >= 0 &&
                    static_cast<size_t>(offset) + static_cast<size_t>(length) <= raw.size()) {
                std::vector<uint8_t> encoded(static_cast<size_t>(length));
                for (int i = 0; i < length; ++i)
                    encoded[static_cast<size_t>(i)] = static_cast<uint8_t>(raw[static_cast<size_t>(offset + i)]);
                port::Image image = port::Image::createImage(encoded);
                Value imageRef = storeImage(ctx, image);
                ctx.trace.imageLoads.push_back(ImageLoad{
                    std::string(ctx.callerLabel), pc, imageRef, "<byte[]>", image.width, image.height});
                return handledValue(imageRef);
            }
        }
    }
    return handledValue(Value::named("image:<invalid-byte-array>"));
}

NativeCallResult nm_image_createImage_II(
    NativeCallContext& ctx,
    uint32_t pc,
    const std::vector<Value>& args) {
    int width = intArg(args, 0);
    int height = intArg(args, 1);
    port::Image image = port::Image::createImage(width, height);
    Value imageRef = storeImage(ctx, image);
    ctx.trace.imageLoads.push_back(ImageLoad{
        std::string(ctx.callerLabel),
        pc,
        imageRef,
        "<generated>",
        image.width,
        image.height,
    });
    return handledValue(imageRef);
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kImageStaticMethods[] = {
    {"createImage", "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;", &nm_image_createImage_String},
    {"createImage", "([BII)Ljavax/microedition/lcdui/Image;",               &nm_image_createImage_BII},
    {"createImage", "(II)Ljavax/microedition/lcdui/Image;",                 &nm_image_createImage_II},
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

NativeLeafFn lookupImageStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kImageStaticMethods,
                      sizeof(kImageStaticMethods) / sizeof(kImageStaticMethods[0]),
                      name, descriptor);
}

NativeCallResult handleImage(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    // Delegate static createImage calls to the same leaves used by the
    // per-class call-site fast path (single source of truth). callerLabel
    // is taken from methodLabel here since the slow-path signature still
    // carries it explicitly.
    if (NativeLeafFn leaf = lookupImageStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }

    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];
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
