#include "native_methods.hpp"

#include "jvm_host.hpp"

#include <algorithm>
#include <cstdlib>

namespace jvmpoc {
namespace {

bool nativeRuntimeClassMatches(const std::string& className) {
    const std::string suffix = "/NativeRuntime";
    return className == "NativeRuntime" ||
           (className.size() >= suffix.size() &&
            className.compare(className.size() - suffix.size(), suffix.size(), suffix) == 0);
}

bool returnsValue(const std::string& descriptor) {
    size_t close = descriptor.find(')');
    return close != std::string::npos && close + 1 < descriptor.size() && descriptor[close + 1] != 'V';
}

std::optional<uint32_t> parseHandle(const Value& value, const std::string& prefix) {
    if (value.text.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    unsigned long parsed = std::strtoul(value.text.c_str() + prefix.size(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> stringId(const Value& value) {
    return parseHandle(value, "str#");
}

bool isStringReceiver(const MethodRef& ref, const Value& receiver) {
    return ref.className == "java/lang/String" || stringId(receiver).has_value();
}

std::string methodName(const MethodRef& ref) {
    return ref.className + "." + ref.name + ref.descriptor;
}

NativeCallResult handledVoid() {
    return NativeCallResult{true, std::nullopt};
}

NativeCallResult handledValue(Value value) {
    return NativeCallResult{true, std::move(value)};
}

uint16_t rgb565(int red, int green, int blue) {
    red = std::max(0, std::min(255, red));
    green = std::max(0, std::min(255, green));
    blue = std::max(0, std::min(255, blue));
    return static_cast<uint16_t>(((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3));
}

int intArg(const std::vector<Value>& args, size_t index, int fallback = 0) {
    if (index >= args.size()) {
        return fallback;
    }
    std::optional<int> parsed = parseIntValue(args[index]);
    return parsed.has_value() ? *parsed : fallback;
}

std::string argText(const std::vector<Value>& args, size_t index) {
    return index < args.size() ? args[index].text : "<missing-arg>";
}

std::string stringArg(const NativeCallContext& ctx, const std::vector<Value>& args, size_t index) {
    if (index >= args.size()) {
        return "";
    }
    std::optional<uint32_t> id = stringId(args[index]);
    auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
    return id.has_value() && strIt != ctx.strings.end() ? strIt->second : args[index].text;
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
        fillRect(ctx, cx, drawY, charWidth, 1);
        fillRect(ctx, cx, drawY + charHeight - 1, charWidth, 1);
        fillRect(ctx, cx, drawY, 1, charHeight);
        fillRect(ctx, cx + charWidth - 1, drawY, 1, charHeight);
    }
}

} // namespace

NativeCallResult handleNativeStaticCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (nativeRuntimeClassMatches(ref.className) && ref.name == "printInt" && ref.descriptor == "(I)V") {
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            methodLabel,
            pc,
            args.empty() ? Value::named("<missing-arg>") : args[0],
        });
        return handledVoid();
    }

    if (nativeRuntimeClassMatches(ref.className) &&
        ref.name == "printString" && ref.descriptor == "(Ljava/lang/String;)V") {
        Value value = args.empty() ? Value::named("<missing-arg>") : args[0];
        std::optional<uint32_t> id = stringId(value);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        ctx.trace.runtimePrints.push_back(RuntimePrint{
            methodLabel,
            pc,
            id.has_value() && strIt != ctx.strings.end()
                ? Value::named(strIt->second)
                : Value::named("<string:" + value.text + ">"),
        });
        return handledVoid();
    }

    if (nativeRuntimeClassMatches(ref.className) && ref.name == "gc" && ref.descriptor == "()V") {
        ctx.collectGarbage(methodLabel + " pc=" + std::to_string(pc));
        return handledVoid();
    }

    if (ref.className == "javax/microedition/lcdui/Display" &&
        ref.name == "getDisplay" &&
        ref.descriptor == "(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;") {
        return handledValue(ctx.displayRef);
    }

    return NativeCallResult{};
}

NativeCallResult handleNativeInstanceCall(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    Value receiver = args.empty() ? Value::named("<missing-receiver>") : args[0];

    if (ref.className == "javax/microedition/midlet/MIDlet" &&
        ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if (ref.className == "javax/microedition/lcdui/Canvas" &&
        ref.name == "<init>" && ref.descriptor == "()V") {
        return handledVoid();
    }

    if (ref.className == "javax/microedition/lcdui/Display" &&
        ref.name == "setCurrent" &&
        ref.descriptor == "(Ljavax/microedition/lcdui/Displayable;)V") {
        ctx.currentDisplayable = args.size() > 1 ? args[1] : Value::named("0");
        ctx.trace.displaySetCurrents.push_back(DisplaySetCurrent{
            methodLabel,
            pc,
            receiver,
            ctx.currentDisplayable,
        });
        return handledVoid();
    }

    if ((ref.name == "getWidth" || ref.name == "getHeight") && ref.descriptor == "()I") {
        const int value = ref.name == "getWidth"
            ? (ctx.host != nullptr ? ctx.host->screenWidth() : 240)
            : (ctx.host != nullptr ? ctx.host->screenHeight() : 320);
        ctx.trace.canvasSizeQueries.push_back(CanvasSizeQuery{
            methodLabel,
            pc,
            receiver,
            methodName(ref),
            value,
        });
        return handledValue(Value::named(std::to_string(value)));
    }

    if (ref.className == "javax/microedition/lcdui/Graphics" &&
        ref.name == "setColor" && ref.descriptor == "(III)V") {
        ctx.graphicsColor = rgb565(intArg(args, 1), intArg(args, 2), intArg(args, 3));
        ctx.trace.graphicsOps.push_back(GraphicsOp{
            methodLabel,
            pc,
            "setColor(" + argText(args, 1) + "," + argText(args, 2) + "," + argText(args, 3) + ")",
        });
        return handledVoid();
    }

    if (ref.className == "javax/microedition/lcdui/Graphics" &&
        ref.name == "fillRect" && ref.descriptor == "(IIII)V") {
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

    if (ref.className == "javax/microedition/lcdui/Graphics" &&
        ref.name == "drawString" && ref.descriptor == "(Ljava/lang/String;III)V") {
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

    if (isStringReceiver(ref, receiver) && ref.name == "length" && ref.descriptor == "()I") {
        std::optional<uint32_t> id = stringId(receiver);
        auto strIt = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
        return handledValue(id.has_value() && strIt != ctx.strings.end()
            ? Value::named(std::to_string(strIt->second.size()))
            : Value::named("<string-length:" + receiver.text + ">"));
    }

    if (isStringReceiver(ref, receiver)) {
        ctx.trace.unsupportedStringCalls.push_back(UnsupportedStringCall{
            methodLabel,
            pc,
            methodName(ref),
            receiver,
        });
        return returnsValue(ref.descriptor)
            ? handledValue(Value::named("<unsupported-string-call:" + ref.name + ">"))
            : handledVoid();
    }

    return NativeCallResult{};
}

} // namespace jvmpoc
