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

static const Value kMissingReceiver = Value::named("<missing-receiver>");

// ---------------------------------------------------------------------------
// Static factories (Image.createImage variants). Reached via invokestatic;
// args[0] is the first argument, not a receiver.
// ---------------------------------------------------------------------------

NativeCallResult nm_createImage_string(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    std::string path = stringArg(ctx, args, 0);
    auto cached = ctx.resourceImages.find(path);
    if (cached != ctx.resourceImages.end()) {
        auto imageIt = ctx.images.find(cached->second);
        if (imageIt != ctx.images.end()) {
            Value imageRef = Value::ofInt(Value::kHandleImgTag | static_cast<int32_t>(cached->second & 0xFFFFFF));
            ctx.trace.imageLoads.push_back(ImageLoad{
                methodLabel, pc, imageRef, path,
                imageIt->second.width, imageIt->second.height,
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
        methodLabel, pc, imageRef, path,
        image.width, image.height,
    });
    return handledValue(imageRef);
}

NativeCallResult nm_createImage_byteArr(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
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
                    methodLabel, pc, imageRef, "<byte[]>", image.width, image.height});
                return handledValue(imageRef);
            }
        }
    }
    return handledValue(Value::named("image:<invalid-byte-array>"));
}

NativeCallResult nm_createImage_ii(
    NativeCallContext& ctx, const std::string& methodLabel, uint32_t pc,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    int width = intArg(args, 0);
    int height = intArg(args, 1);
    port::Image image = port::Image::createImage(width, height);
    Value imageRef = storeImage(ctx, image);
    ctx.trace.imageLoads.push_back(ImageLoad{
        methodLabel, pc, imageRef, "<generated>",
        image.width, image.height,
    });
    return handledValue(imageRef);
}

// ---------------------------------------------------------------------------
// Instance methods. Reached via invokevirtual / receiver-type fallback.
// args[0] is the receiver.
// ---------------------------------------------------------------------------

NativeCallResult nm_image_getWidth(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    std::optional<uint32_t> id = imageId(receiver);
    auto imageIt = id.has_value() ? ctx.images.find(*id) : ctx.images.end();
    return handledValue(Value::ofInt(
        id.has_value() && imageIt != ctx.images.end() ? imageIt->second.getWidth() : 0));
}

NativeCallResult nm_image_getHeight(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    std::optional<uint32_t> id = imageId(receiver);
    auto imageIt = id.has_value() ? ctx.images.find(*id) : ctx.images.end();
    return handledValue(Value::ofInt(
        id.has_value() && imageIt != ctx.images.end() ? imageIt->second.getHeight() : 0));
}

NativeCallResult nm_image_getGraphics(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const Value& receiver = args.empty() ? kMissingReceiver : args[0];
    std::optional<uint32_t> id = imageId(receiver);
    return id.has_value()
        ? handledValue(Value::ofInt(Value::kHandleGfxTag | static_cast<int32_t>(*id & 0xFFFFFF)))
        : handledValue(Value::named("graphics:<missing-image>"));
}

// ---------------------------------------------------------------------------
// Combined table. Both the static and instance leaf resolvers consult this
// single source of truth — Java semantics ensure static methods are only
// reached via invokestatic and instance methods via invokevirtual, so there's
// no risk of the wrong category binding.
// ---------------------------------------------------------------------------
const NativeMethodEntry kImageMethods[] = {
    // Static factories
    {"createImage", "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;", &nm_createImage_string},
    {"createImage", "([BII)Ljavax/microedition/lcdui/Image;",                &nm_createImage_byteArr},
    {"createImage", "(II)Ljavax/microedition/lcdui/Image;",                  &nm_createImage_ii},
    // Instance
    {"getWidth",    "()I",                                                   &nm_image_getWidth},
    {"getHeight",   "()I",                                                   &nm_image_getHeight},
    {"getGraphics", "()Ljavax/microedition/lcdui/Graphics;",                 &nm_image_getGraphics},
};

} // namespace

NativeMethodFn resolveImageMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kImageMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleImage(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveImageMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
