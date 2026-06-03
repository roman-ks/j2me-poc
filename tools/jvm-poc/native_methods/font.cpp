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

NativeCallResult nm_font_getDefaultFont(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(defaultFontRef());
}

NativeCallResult nm_font_getFont(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(defaultFontRef());
}

NativeCallResult nm_font_getHeight(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(port::kBitmapFontHeight));
}

NativeCallResult nm_font_getBaselinePosition(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(port::kBitmapFontBaseline));
}

NativeCallResult nm_font_getFace(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));   // FACE_SYSTEM
}

NativeCallResult nm_font_getStyle(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));   // STYLE_PLAIN
}

NativeCallResult nm_font_getSize(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(8));   // SIZE_SMALL — matches our 5x7 glyph
}

NativeCallResult nm_font_isPlain(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(1));
}

NativeCallResult nm_font_isStyleFalse(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(0));
}

NativeCallResult nm_font_charWidth(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& /*args*/) {
    return handledValue(Value::ofInt(port::kBitmapFontAdvance));
}

NativeCallResult nm_font_charsWidth(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& args) {
    const int length = intArg(args, 3);
    return handledValue(Value::ofInt(stringWidthChars(length)));
}

NativeCallResult nm_font_stringWidth(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string text = stringArg(ctx, args, 1);
    return handledValue(Value::ofInt(stringWidthChars(utf8CharCount(text))));
}

NativeCallResult nm_font_substringWidth(
    NativeCallContext& /*ctx*/, uint32_t /*pc*/, const std::vector<Value>& args) {
    const int length = intArg(args, 3);
    return handledValue(Value::ofInt(stringWidthChars(length)));
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kFontStaticMethods[] = {
    {"getDefaultFont", "()Ljavax/microedition/lcdui/Font;",    &nm_font_getDefaultFont},
    {"getFont",        "(III)Ljavax/microedition/lcdui/Font;", &nm_font_getFont},
    {"getFont",        "(I)Ljavax/microedition/lcdui/Font;",   &nm_font_getFont},
};

constexpr NativeMethodEntry kFontInstanceMethods[] = {
    {"getHeight",           "()I",                           &nm_font_getHeight},
    {"getBaselinePosition", "()I",                           &nm_font_getBaselinePosition},
    {"getFace",             "()I",                           &nm_font_getFace},
    {"getStyle",            "()I",                           &nm_font_getStyle},
    {"getSize",             "()I",                           &nm_font_getSize},
    {"isPlain",             "()Z",                           &nm_font_isPlain},
    {"isBold",              "()Z",                           &nm_font_isStyleFalse},
    {"isItalic",            "()Z",                           &nm_font_isStyleFalse},
    {"isUnderlined",        "()Z",                           &nm_font_isStyleFalse},
    {"charWidth",           "(C)I",                          &nm_font_charWidth},
    {"charsWidth",          "([CII)I",                       &nm_font_charsWidth},
    {"stringWidth",         "(Ljava/lang/String;)I",         &nm_font_stringWidth},
    {"substringWidth",      "(Ljava/lang/String;II)I",       &nm_font_substringWidth},
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

NativeLeafFn lookupFontStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kFontStaticMethods,
                      sizeof(kFontStaticMethods) / sizeof(kFontStaticMethods[0]),
                      name, descriptor);
}

NativeLeafFn lookupFontInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kFontInstanceMethods,
                      sizeof(kFontInstanceMethods) / sizeof(kFontInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleFont(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRefView& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupFontStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    if (NativeLeafFn leaf = lookupFontInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
