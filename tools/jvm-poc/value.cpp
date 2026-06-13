#include "value.hpp"

#include <climits>
#include <stdexcept>

namespace jvmpoc {

// ---------- Value::named -------------------------------------------------
// Tries to store plain decimal integers as int32/int64 to avoid heap allocs.
// Anything else (sentinel strings, class/stream handles) is heap-allocated.

Value Value::named(std::string s) {
    if (!s.empty()) {
        const char* p = s.c_str();
        const size_t len = s.size();
        size_t start = (p[0] == '-') ? 1u : 0u;
        if (start < len) {
            bool allDigits = true;
            for (size_t i = start; i < len; ++i) {
                if (p[i] < '0' || p[i] > '9') { allDigits = false; break; }
            }
            if (allDigits) {
                try {
                    const long long v = std::stoll(s);
                    if (v >= INT32_MIN && v <= INT32_MAX) return ofInt(static_cast<int32_t>(v));
                    return ofLong(static_cast<int64_t>(v));
                } catch (...) {}
            }
        }
    }
    Value r;
    r.tag = Tag::kStr;
    r.str = new std::string(std::move(s));
    return r;
}

// ---------- Value::asText ------------------------------------------------

std::string Value::asText() const {
    if (tag == Tag::kInt) {
        const int32_t tag32 = i32 & kHandleTagMask;
        const int32_t id    = i32 & kHandleIdMask;
        if      (tag32 == kHandleObjTag) return "obj#"            + std::to_string(id);
        else if (tag32 == kHandleArrTag) return "arr#"            + std::to_string(id);
        else if (tag32 == kHandleImgTag) return "image#"          + std::to_string(id);
        else if (tag32 == kHandleGfxTag) return "graphics:image#" + std::to_string(id);
        else if (tag32 == kHandleFontTag)    return "font#"    + std::to_string(id);
        else if (tag32 == kHandleDisplayTag) return "display#" + std::to_string(id);
        return std::to_string(i32);
    }
    if (tag == Tag::kLong)  return "long#" + std::to_string(i64);
    if (tag == Tag::kStr)   return "str#" + *str;
    return "none";  // kNone / uninitialized
}

// ---------- parseLongValue -----------------------------------------------

std::optional<long long> parseLongValue(const Value& value) {
    if (value.tag == Value::Tag::kInt)  return static_cast<long long>(value.i32);
    if (value.tag == Value::Tag::kLong) return value.i64;
    if (value.tag == Value::Tag::kStr) {
        const std::string& s = *value.str;
        if (s.empty()) return std::nullopt;
        size_t pos = (s[0] == '-') ? 1u : 0u;
        if (pos == s.size()) return std::nullopt;
        for (; pos < s.size(); ++pos) {
            if (s[pos] < '0' || s[pos] > '9') return std::nullopt;
        }
        return std::stoll(s);
    }
    return std::nullopt;  // kNone
}

// ---------- parseIntValue ------------------------------------------------

std::optional<int> parseIntValue(const Value& value) {
    if (value.tag == Value::Tag::kInt) return value.i32;
    if (value.tag == Value::Tag::kLong) {
        if (value.i64 >= INT32_MIN && value.i64 <= INT32_MAX)
            return static_cast<int>(value.i64);
        return std::nullopt;
    }
    const std::optional<long long> parsed = parseLongValue(value);
    if (!parsed || *parsed < INT32_MIN || *parsed > INT32_MAX) return std::nullopt;
    return static_cast<int>(*parsed);
}

} // namespace jvmpoc
