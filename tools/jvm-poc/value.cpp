#include "value.hpp"

#include <climits>
#include <stdexcept>

namespace jvmpoc {

// ---------- Value::named -------------------------------------------------
// Tries to store plain decimal integers as int32/int64 to avoid heap allocs.

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
    r.data = std::move(s);
    return r;
}

// ---------- Value::asText ------------------------------------------------

std::string Value::asText() const {
    if (const auto* i = std::get_if<int32_t>(&data)) return std::to_string(*i);
    if (const auto* l = std::get_if<int64_t>(&data)) return std::to_string(*l);
    if (const auto* s = std::get_if<std::string>(&data)) return *s;
    return {};  // monostate / uninitialized
}

// ---------- parseLongValue -----------------------------------------------

std::optional<long long> parseLongValue(const Value& value) {
    if (const auto* i = std::get_if<int32_t>(&value.data)) {
        return static_cast<long long>(*i);
    }
    if (const auto* l = std::get_if<int64_t>(&value.data)) {
        return *l;
    }
    // String fallback (only for unexpected text-encoded numbers)
    if (const auto* s = std::get_if<std::string>(&value.data)) {
        if (s->empty()) return std::nullopt;
        size_t pos = 0;
        if ((*s)[0] == '-') pos = 1;
        if (pos == s->size()) return std::nullopt;
        for (; pos < s->size(); ++pos) {
            if ((*s)[pos] < '0' || (*s)[pos] > '9') return std::nullopt;
        }
        return std::stoll(*s);
    }
    return std::nullopt;  // monostate
}

// ---------- parseIntValue ------------------------------------------------

std::optional<int> parseIntValue(const Value& value) {
    if (const auto* i = std::get_if<int32_t>(&value.data)) {
        return *i;
    }
    if (const auto* l = std::get_if<int64_t>(&value.data)) {
        if (*l >= INT32_MIN && *l <= INT32_MAX) return static_cast<int>(*l);
        return std::nullopt;
    }
    // String fallback
    const std::optional<long long> parsed = parseLongValue(value);
    if (!parsed || *parsed < INT32_MIN || *parsed > INT32_MAX) return std::nullopt;
    return static_cast<int>(*parsed);
}

} // namespace jvmpoc
