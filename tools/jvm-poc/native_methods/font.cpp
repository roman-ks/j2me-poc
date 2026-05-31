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

// ---------------------------------------------------------------------------
// Leaf method handlers. Several methods share bodies (getFont overloads,
// charsWidth/substringWidth, the boolean style queries) — listed once here
// and referenced from multiple table rows below.
// ---------------------------------------------------------------------------

// Used by getDefaultFont()L and both getFont overloads — all return the same
// singleton font handle.
NativeCallResult nm_font_default(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(defaultFontRef());
}

NativeCallResult nm_font_getHeight(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(port::kBitmapFontHeight));
}

NativeCallResult nm_font_getBaselinePosition(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(port::kBitmapFontBaseline));
}

NativeCallResult nm_font_getFace(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));   // FACE_SYSTEM
}

NativeCallResult nm_font_getStyle(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));   // STYLE_PLAIN
}

NativeCallResult nm_font_getSize(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(8));   // SIZE_SMALL — matches our 5x7 glyph
}

NativeCallResult nm_font_isPlain(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(1));
}

// Used by isBold, isItalic, isUnderlined — all three always return false in
// our single-font runtime.
NativeCallResult nm_font_isFalse(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));
}

NativeCallResult nm_font_charWidth(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& /*args*/) {
    // Use advance so Σ charWidth(c) ≈ stringWidth (off-by-one matches the
    // trailing-gap convention Canvas.drawString uses).
    return handledValue(Value::ofInt(port::kBitmapFontAdvance));
}

// Used by both charsWidth([CII)I and substringWidth(Ljava/lang/String;II)I —
// both take length at arg slot 3.
NativeCallResult nm_font_widthFromArg3(
    NativeCallContext& /*ctx*/, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const int length = intArg(args, 3);
    return handledValue(Value::ofInt(stringWidthChars(length)));
}

NativeCallResult nm_font_stringWidth(
    NativeCallContext& ctx, const std::string& /*methodLabel*/, uint32_t /*pc*/,
    const MethodRef& /*ref*/, const std::vector<Value>& args) {
    const std::string text = stringArg(ctx, args, 1);
    return handledValue(Value::ofInt(stringWidthChars(utf8CharCount(text))));
}

// ---------------------------------------------------------------------------
// Method table. Font has both static factories (getDefaultFont, getFont) and
// instance queries; one table serves both — Java bytecode invokestatic /
// invokevirtual binds correctly per method.
// ---------------------------------------------------------------------------
const NativeMethodEntry kFontMethods[] = {
    // Static factories — all share the same body (return the one physical font)
    {"getDefaultFont",     "()Ljavax/microedition/lcdui/Font;",      &nm_font_default},
    {"getFont",            "(III)Ljavax/microedition/lcdui/Font;",   &nm_font_default},
    {"getFont",            "(I)Ljavax/microedition/lcdui/Font;",     &nm_font_default},
    // Instance metrics
    {"getHeight",          "()I",                                    &nm_font_getHeight},
    {"getBaselinePosition","()I",                                    &nm_font_getBaselinePosition},
    {"getFace",            "()I",                                    &nm_font_getFace},
    {"getStyle",           "()I",                                    &nm_font_getStyle},
    {"getSize",            "()I",                                    &nm_font_getSize},
    {"isPlain",            "()Z",                                    &nm_font_isPlain},
    {"isBold",             "()Z",                                    &nm_font_isFalse},
    {"isItalic",           "()Z",                                    &nm_font_isFalse},
    {"isUnderlined",       "()Z",                                    &nm_font_isFalse},
    {"charWidth",          "(C)I",                                   &nm_font_charWidth},
    {"charsWidth",         "([CII)I",                                &nm_font_widthFromArg3},
    {"substringWidth",     "(Ljava/lang/String;II)I",                &nm_font_widthFromArg3},
    {"stringWidth",        "(Ljava/lang/String;)I",                  &nm_font_stringWidth},
};

} // namespace

NativeMethodFn resolveFontMethod(const std::string& name, const std::string& descriptor) {
    for (const auto& e : kFontMethods) {
        if (name == e.name && descriptor == e.descriptor) return e.fn;
    }
    return nullptr;
}

NativeCallResult handleFont(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    NativeMethodFn fn = resolveFontMethod(ref.name, ref.descriptor);
    if (fn != nullptr) return fn(ctx, methodLabel, pc, ref, args);
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
