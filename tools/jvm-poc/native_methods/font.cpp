#include "handlers.hpp"

#include "helpers.hpp"
#include "../j2me_port/BitmapFont5x7.hpp"

namespace jvmpoc::native_methods {

namespace {

// Singleton handle for the one physical font (port::kBitmapFont5x7). All
// getFont/getDefaultFont variants return this same value — the runtime owns no
// other glyph data. Matches the kStr-sentinel pattern used by displayRef.
Value defaultFontRef() {
    return Value::named("font:default");
}

int utf8CharCount(const std::string& s) {
    // Count code points (not bytes) for width math. The bitmap font only has
    // ASCII glyphs; non-ASCII bytes map to '?' but still occupy one cell.
    int count = 0;
    for (size_t i = 0; i < s.size(); ) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80) == 0) step = 1;
        else if ((c & 0xE0) == 0xC0) step = 2;
        else if ((c & 0xF0) == 0xE0) step = 3;
        else if ((c & 0xF8) == 0xF0) step = 4;
        i += step;
        ++count;
    }
    return count;
}

int stringWidthChars(int charCount) {
    if (charCount <= 0) return 0;
    return charCount * port::kBitmapFontAdvance - 1;
}

} // namespace

NativeCallResult handleFont(
    NativeCallContext& ctx,
    const std::string& /*methodLabel*/,
    uint32_t /*pc*/,
    const MethodRef& ref,
    const std::vector<Value>& args) {

    if (ref.name == "getDefaultFont" && ref.descriptor == "()Ljavax/microedition/lcdui/Font;") {
        return handledValue(defaultFontRef());
    }
    if (ref.name == "getFont" &&
        (ref.descriptor == "(III)Ljavax/microedition/lcdui/Font;" ||
         ref.descriptor == "(I)Ljavax/microedition/lcdui/Font;")) {
        return handledValue(defaultFontRef());
    }

    if (ref.name == "getHeight" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(port::kBitmapFontHeight));
    }
    if (ref.name == "getBaselinePosition" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(port::kBitmapFontBaseline));
    }

    if (ref.name == "getFace" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(0));   // FACE_SYSTEM
    }
    if (ref.name == "getStyle" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(0));   // STYLE_PLAIN
    }
    if (ref.name == "getSize" && ref.descriptor == "()I") {
        return handledValue(Value::ofInt(8));   // SIZE_SMALL — matches our 5x7 glyph
    }

    if (ref.name == "isPlain" && ref.descriptor == "()Z") {
        return handledValue(Value::ofInt(1));
    }
    if ((ref.name == "isBold" || ref.name == "isItalic" || ref.name == "isUnderlined") &&
        ref.descriptor == "()Z") {
        return handledValue(Value::ofInt(0));
    }

    if (ref.name == "charWidth" && ref.descriptor == "(C)I") {
        // Use advance so Σ charWidth(c) ≈ stringWidth (off-by-one matches the
        // trailing-gap convention Canvas.drawString uses).
        return handledValue(Value::ofInt(port::kBitmapFontAdvance));
    }
    if (ref.name == "charsWidth" && ref.descriptor == "([CII)I") {
        const int length = intArg(args, 3);
        return handledValue(Value::ofInt(stringWidthChars(length)));
    }
    if (ref.name == "stringWidth" && ref.descriptor == "(Ljava/lang/String;)I") {
        const std::string text = stringArg(ctx, args, 1);
        return handledValue(Value::ofInt(stringWidthChars(utf8CharCount(text))));
    }
    if (ref.name == "substringWidth" && ref.descriptor == "(Ljava/lang/String;II)I") {
        const int length = intArg(args, 3);
        return handledValue(Value::ofInt(stringWidthChars(length)));
    }

    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
