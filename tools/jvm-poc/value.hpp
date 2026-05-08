#pragma once

#include <climits>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace jvmpoc {

// Value represents a JVM operand-stack / local-variable slot.
//
// Variant arms:
//   std::monostate — uninitialized; only in freshly-allocated Frame locals
//   int32_t        — boolean, byte, char, short, int, and null-ref (== 0)
//   int64_t        — long
//   std::string    — object/array/image/class handles ("obj#N", "arr#N", …)
//                    and interpreter debug strings
//
// float and double are not used by the target MIDlets yet.
// To add them: append `float` / `double` to the variant and add ofFloat/ofDouble
// factories below — no other code needs touching.
struct Value {
    using Data = std::variant<std::monostate, int32_t, int64_t, std::string>;
    Data data;

    // Default: uninitialized slot (detected by Frame::local())
    Value() = default;

// H1: tagged-int handle encoding.
// Object/array/image handles are stored as int32_t with the ID in bits 0-23
// and a type tag in bits 24-31.  Plain JVM integers have tag 0x00 (values < 16M
// are safe for the target MIDlets; colours top out at 0x00FFFFFF).
// Tags 0x01-0x04 are reserved; all others are plain integers.
static constexpr int32_t kHandleObjTag = 0x01 << 24;  // object ref  (obj#N)
static constexpr int32_t kHandleArrTag = 0x02 << 24;  // array ref   (arr#N)
static constexpr int32_t kHandleImgTag = 0x03 << 24;  // image ref   (image#N)
static constexpr int32_t kHandleGfxTag = 0x04 << 24;  // gfx ref     (graphics:image#N)
static constexpr int32_t kHandleTagMask = static_cast<int32_t>(0xFF000000);
static constexpr int32_t kHandleIdMask  = 0x00FFFFFF;

// Explicit numeric factories — use these in hot paths to avoid any string work
    static Value ofInt(int32_t v)  { Value r; r.data = v; return r; }
    static Value ofLong(int64_t v) { Value r; r.data = v; return r; }

    // Smart factory: recognises plain decimal integer strings and stores them
    // as int32/int64 directly; everything else (handles, debug strings) becomes
    // a std::string variant. All existing Value::named("42") call-sites continue
    // to work correctly with zero string heap allocation.
    static Value named(std::string s);

    // String representation for debug output and handle prefix checks
    std::string asText() const;

    // True when this slot has been written (not default-constructed)
    bool isInitialized() const {
        return !std::holds_alternative<std::monostate>(data);
    }

    // True for integer zero or null reference
    bool isNull() const {
        const auto* i = std::get_if<int32_t>(&data);
        return i != nullptr && *i == 0;
    }
};

std::optional<long long> parseLongValue(const Value& value);
std::optional<int>       parseIntValue(const Value& value);

} // namespace jvmpoc
