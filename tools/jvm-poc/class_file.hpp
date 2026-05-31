#pragma once

#include "value.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jvmpoc {

struct NativeCallContext;
struct NativeCallResult;

// Native-method leaf function pointer. Hot-path signature: no methodLabel,
// no MethodRef — both eliminated by per-call-site resolution. The leaf is
// called directly from the interpreter after a CallSiteEntry hit.
using NativeLeafFn = NativeCallResult(*)(
    NativeCallContext& ctx,
    uint32_t pc,
    const std::vector<Value>& args);

// One slot in a ClassFile's per-callsite resolution cache. Indexed via
// ClassFile::cpToSite[cpIdx]. See docs/per-class-call-site-cache.plan.md.
struct CallSiteEntry {
    enum Kind : uint8_t {
        kUncached = 0,     // not yet resolved; slow path runs and populates
        kNative = 1,       // native leaf in nativeFn
        kJava = 2,         // Java method in targetClass/targetMethod
        kSlowCascade = 3,  // dispatched by receiver type (currently: Image only)
        kNotFound = 4,     // resolution failed; don't retry
    };

    Kind kind = kUncached;
    uint8_t argSlots = 0;             // does NOT include 'this' for virtual

    // --- Native path ---
    NativeLeafFn nativeFn = nullptr;

    // --- Java path ---
    const struct ClassFile* targetClass = nullptr;
    const struct MethodInfo* targetMethod = nullptr;

    // --- Virtual-only PIC slot (used by 0xb6/0xb9, ignored by static/special) ---
    const struct ClassFile* receiverClass = nullptr;
};

enum CpTag : uint8_t {
    CpUtf8 = 1,
    CpInteger = 3,
    CpFloat = 4,
    CpLong = 5,
    CpDouble = 6,
    CpClass = 7,
    CpString = 8,
    CpFieldref = 9,
    CpMethodref = 10,
    CpInterfaceMethodref = 11,
    CpNameAndType = 12,
    CpMethodHandle = 15,
    CpMethodType = 16,
    CpInvokeDynamic = 18,
};

struct CpEntry {
    uint8_t tag = 0;
    std::string utf8;
    uint16_t a = 0;
    uint16_t b = 0;
    int32_t intValue = 0;
    int64_t longValue = 0;
};

struct FieldInfo {
    uint16_t access = 0;
    std::string name;
    std::string descriptor;
};

struct MethodInfo {
    struct LocalVariable {
        uint16_t startPc = 0;
        uint16_t length = 0;
        std::string name;
        std::string descriptor;
        uint16_t index = 0;
    };

    struct ExceptionHandler {
        uint16_t startPc = 0;
        uint16_t endPc = 0;
        uint16_t handlerPc = 0;
        uint16_t catchType = 0;
        std::string catchClass;
    };

    uint16_t access = 0;
    std::string name;
    std::string descriptor;
    bool hasCode = false;
    uint16_t maxStack = 0;
    uint16_t maxLocals = 0;
    uint32_t codeLength = 0;
    std::vector<uint8_t> code;
    std::vector<ExceptionHandler> exceptionHandlers;
    std::vector<LocalVariable> locals;
};

struct ClassFile {
    uint16_t minor = 0;
    uint16_t major = 0;
    uint16_t access = 0;
    std::string thisClass;
    std::string superClass;
    std::vector<std::string> interfaces;
    std::vector<FieldInfo> fields;
    std::vector<MethodInfo> methods;
    std::vector<std::string> attributes;
    std::vector<CpEntry> cp;

    // Per-callsite resolution cache. CP-index → packed site index. Sized to
    // (max methodref cpIdx + 1) and filled at parse time; 0xFFFF means "this
    // CP slot is not a method-ref". See docs/per-class-call-site-cache.plan.md
    // for layout rationale (Option C indirection table).
    std::vector<uint16_t> cpToSite;

    // Packed: one CallSiteEntry per Methodref / InterfaceMethodref CP entry.
    // mutable so dispatch can populate lazily through a const ClassFile&.
    mutable std::vector<CallSiteEntry> sites;
};

struct MethodRef {
    std::string className;
    std::string name;
    std::string descriptor;
};

struct FieldRef {
    std::string className;
    std::string name;
    std::string descriptor;
};

bool hasAccess(uint16_t flags, uint16_t bit);
std::string accessText(uint16_t flags, bool method);
size_t fieldSlots(const std::string& descriptor);
size_t argumentSlots(const MethodInfo& method);
std::string localNameAt(const MethodInfo& method, uint16_t index, uint32_t pc);
MethodRef resolveMethodRef(const ClassFile& cls, uint16_t index);
FieldRef resolveFieldRef(const ClassFile& cls, uint16_t index);
std::string resolveClassRef(const ClassFile& cls, uint16_t index);
std::string resolveStringConstant(const ClassFile& cls, uint16_t index);
int32_t resolveIntegerConstant(const ClassFile& cls, uint16_t index);
int64_t resolveLongConstant(const ClassFile& cls, uint16_t index);

ClassFile parseClassFile(const std::string& path);

} // namespace jvmpoc
