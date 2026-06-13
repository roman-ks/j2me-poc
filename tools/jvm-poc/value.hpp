#pragma once

#include <climits>
#include <cstdint>
#include <optional>
#include <string>

namespace jvmpoc {

// Value represents a JVM operand-stack / local-variable slot.
//
// H3: flat tagged union — replaces std::variant to reduce sizeof(Value).
// On ESP32 (32-bit, 4-byte int64_t alignment): sizeof(Value) = 12 bytes
// vs ~16 bytes for std::variant<monostate,int32_t,int64_t,string>.
//
// Tag arms:
//   kNone   — uninitialized; only in freshly-allocated Frame locals
//   kInt    — boolean, byte, char, short, int, null-ref (== 0), and tagged handles (H1)
//   kLong   — long
//   kStr    — debug/sentinel strings ("class:Foo", "<divide-by-zero>", …)
//             held as heap-allocated std::string* (owned, deleted in destructor)
//
// H1 tagged-int handle encoding (tag kInt, high byte = handle type):
//   0x01xxxxxx = object ref   (obj#N)
//   0x02xxxxxx = array ref    (arr#N)
//   0x03xxxxxx = image ref    (image#N)
//   0x04xxxxxx = gfx ref      (graphics:image#N)
struct Value {
    enum class Tag : int32_t { kNone = 0, kInt = 1, kLong = 2, kStr = 3 };

    Tag tag = Tag::kNone;
    union {
        int32_t  i32;
        int64_t  i64;
        std::string* str;  // owned; deleted in destructor (kStr arm only)
    };

    // Default: uninitialized slot
    Value() : tag(Tag::kNone), i32(0) {}

    // Copy and move — required because kStr arm owns a heap string.
    Value(const Value& o) : tag(Tag::kNone), i32(0) { *this = o; }
    Value(Value&& o) noexcept : tag(Tag::kNone), i32(0) { *this = std::move(o); }
    ~Value() { if (tag == Tag::kStr) delete str; }

    Value& operator=(const Value& o) {
        if (this == &o) return *this;
        if (tag == Tag::kStr) { delete str; str = nullptr; }
        tag = o.tag;
        if (o.tag == Tag::kStr) str = new std::string(*o.str);
        else                    i64 = o.i64;  // copy both 32- and 64-bit arms
        return *this;
    }
    Value& operator=(Value&& o) noexcept {
        if (this == &o) return *this;
        if (tag == Tag::kStr) { delete str; str = nullptr; }
        tag = o.tag;
        i64 = o.i64;  // transfers pointer for kStr (o must be cleared)
        o.tag = Tag::kNone;
        o.i32 = 0;
        return *this;
    }

    // H1: tagged-int handle encoding constants.
    static constexpr int32_t kHandleObjTag = 0x01 << 24;
    static constexpr int32_t kHandleArrTag = 0x02 << 24;
    static constexpr int32_t kHandleImgTag = 0x03 << 24;
    static constexpr int32_t kHandleGfxTag = 0x04 << 24;
    // Font/Display are singletons represented as tagged-int handles (not kStr
    // sentinels) so they round-trip through any handle-aware storage.
    static constexpr int32_t kHandleFontTag    = 0x05 << 24;
    static constexpr int32_t kHandleDisplayTag = 0x06 << 24;
    static constexpr int32_t kHandleTagMask = static_cast<int32_t>(0xFF000000);
    static constexpr int32_t kHandleIdMask  = 0x00FFFFFF;
    static constexpr int32_t kFontHandle    = kHandleFontTag    | 1; // singleton
    static constexpr int32_t kDisplayHandle = kHandleDisplayTag | 1; // singleton

    // Factories
    static Value ofInt(int32_t v)  { Value r; r.tag = Tag::kInt;  r.i32 = v; return r; }
    static Value ofLong(int64_t v) { Value r; r.tag = Tag::kLong; r.i64 = v; return r; }
    static Value named(std::string s);

    std::string asText() const;

    bool isInitialized() const { return tag != Tag::kNone; }

    bool isNull() const { return tag == Tag::kInt && i32 == 0; }
};

std::optional<long long> parseLongValue(const Value& value);
std::optional<int>       parseIntValue(const Value& value);

} // namespace jvmpoc
