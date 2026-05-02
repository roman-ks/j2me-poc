#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jvmpoc {

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

    uint16_t access = 0;
    std::string name;
    std::string descriptor;
    bool hasCode = false;
    uint16_t maxStack = 0;
    uint16_t maxLocals = 0;
    uint32_t codeLength = 0;
    std::vector<uint8_t> code;
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
