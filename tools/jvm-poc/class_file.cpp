#include "class_file.hpp"

#include "reader.hpp"

#include <fstream>
#include <stdexcept>

namespace jvmpoc {
namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::string utf8(const std::vector<CpEntry>& cp, uint16_t index) {
    if (index == 0 || index >= cp.size() || cp[index].tag != CpUtf8) {
        throw std::runtime_error("bad UTF8 constant pool reference #" + std::to_string(index));
    }
    return cp[index].utf8;
}

std::string className(const std::vector<CpEntry>& cp, uint16_t index) {
    if (index == 0) {
        return "";
    }
    if (index >= cp.size() || cp[index].tag != CpClass) {
        throw std::runtime_error("bad Class constant pool reference #" + std::to_string(index));
    }
    return utf8(cp, cp[index].a);
}

size_t typeSlotsAt(const std::string& desc, size_t& pos) {
    char c = desc.at(pos++);
    if (c == 'J' || c == 'D') {
        return 2;
    }
    if (c == 'L') {
        while (pos < desc.size() && desc[pos++] != ';') {
        }
        return 1;
    }
    if (c == '[') {
        while (pos < desc.size() && desc[pos] == '[') {
            ++pos;
        }
        if (pos < desc.size() && desc[pos] == 'L') {
            while (pos < desc.size() && desc[pos++] != ';') {
            }
        } else if (pos < desc.size()) {
            ++pos;
        }
        return 1;
    }
    return 1;
}

void skipAttributes(Reader& r, const std::vector<CpEntry>& cp) {
    uint16_t count = r.u2();
    for (uint16_t i = 0; i < count; ++i) {
        (void)utf8(cp, r.u2());
        r.skip(r.u4());
    }
}

FieldInfo readField(Reader& r, const std::vector<CpEntry>& cp) {
    FieldInfo field;
    field.access = r.u2();
    field.name = utf8(cp, r.u2());
    field.descriptor = utf8(cp, r.u2());
    skipAttributes(r, cp);
    return field;
}

MethodInfo readMethod(Reader& r, const std::vector<CpEntry>& cp) {
    MethodInfo method;
    method.access = r.u2();
    method.name = utf8(cp, r.u2());
    method.descriptor = utf8(cp, r.u2());

    uint16_t attributesCount = r.u2();
    for (uint16_t i = 0; i < attributesCount; ++i) {
        std::string attrName = utf8(cp, r.u2());
        uint32_t attrLen = r.u4();
        if (attrName != "Code") {
            r.skip(attrLen);
            continue;
        }

        Reader code(r.bytes(attrLen));
        method.hasCode = true;
        method.maxStack = code.u2();
        method.maxLocals = code.u2();
        method.codeLength = code.u4();
        method.code = code.bytes(method.codeLength);

        uint16_t exceptionTableLength = code.u2();
        code.skip(static_cast<size_t>(exceptionTableLength) * 8);

        uint16_t codeAttributesCount = code.u2();
        for (uint16_t j = 0; j < codeAttributesCount; ++j) {
            std::string codeAttrName = utf8(cp, code.u2());
            uint32_t codeAttrLen = code.u4();
            if (codeAttrName != "LocalVariableTable") {
                code.skip(codeAttrLen);
                continue;
            }

            Reader lvt(code.bytes(codeAttrLen));
            uint16_t localCount = lvt.u2();
            for (uint16_t k = 0; k < localCount; ++k) {
                MethodInfo::LocalVariable local;
                local.startPc = lvt.u2();
                local.length = lvt.u2();
                local.name = utf8(cp, lvt.u2());
                local.descriptor = utf8(cp, lvt.u2());
                local.index = lvt.u2();
                method.locals.push_back(local);
            }
        }
    }

    return method;
}

} // namespace

bool hasAccess(uint16_t flags, uint16_t bit) {
    return (flags & bit) != 0;
}

std::string accessText(uint16_t flags, bool method) {
    std::string out;
    auto add = [&](const char* s) {
        if (!out.empty()) {
            out += ' ';
        }
        out += s;
    };

    if (hasAccess(flags, 0x0001)) add("public");
    if (hasAccess(flags, 0x0002)) add("private");
    if (hasAccess(flags, 0x0004)) add("protected");
    if (hasAccess(flags, 0x0008)) add("static");
    if (hasAccess(flags, 0x0010)) add("final");
    if (method && hasAccess(flags, 0x0020)) add("synchronized");
    if (!method && hasAccess(flags, 0x0040)) add("volatile");
    if (method && hasAccess(flags, 0x0040)) add("bridge");
    if (!method && hasAccess(flags, 0x0080)) add("transient");
    if (method && hasAccess(flags, 0x0080)) add("varargs");
    if (hasAccess(flags, 0x0400)) add("abstract");
    if (hasAccess(flags, 0x1000)) add("synthetic");

    return out.empty() ? "package" : out;
}

size_t fieldSlots(const std::string& descriptor) {
    size_t pos = 0;
    return typeSlotsAt(descriptor, pos);
}

size_t argumentSlots(const MethodInfo& method) {
    size_t slots = hasAccess(method.access, 0x0008) ? 0 : 1;
    size_t pos = 0;
    if (method.descriptor.empty() || method.descriptor[pos++] != '(') {
        return slots;
    }

    while (pos < method.descriptor.size() && method.descriptor[pos] != ')') {
        slots += typeSlotsAt(method.descriptor, pos);
    }
    return slots;
}

std::string localNameAt(const MethodInfo& method, uint16_t index, uint32_t pc) {
    for (const MethodInfo::LocalVariable& local : method.locals) {
        if (local.index == index && pc >= local.startPc && pc < local.startPc + local.length) {
            return local.name;
        }
    }
    for (const MethodInfo::LocalVariable& local : method.locals) {
        if (local.index == index) {
            return local.name;
        }
    }
    return "local" + std::to_string(index);
}

ClassFile parseClassFile(const std::string& path) {
    Reader r(readFile(path));
    if (r.u4() != 0xCAFEBABE) {
        throw std::runtime_error(path + " is not a Java class file");
    }

    ClassFile cls;
    cls.minor = r.u2();
    cls.major = r.u2();

    uint16_t cpCount = r.u2();
    cls.cp.resize(cpCount);
    for (uint16_t i = 1; i < cpCount; ++i) {
        CpEntry entry;
        entry.tag = r.u1();
        switch (entry.tag) {
            case CpUtf8: {
                uint16_t len = r.u2();
                std::vector<uint8_t> bytes = r.bytes(len);
                entry.utf8.assign(bytes.begin(), bytes.end());
                break;
            }
            case CpInteger:
            case CpFloat:
                r.skip(4);
                break;
            case CpLong:
            case CpDouble:
                r.skip(8);
                cls.cp[i] = entry;
                ++i;
                continue;
            case CpClass:
            case CpString:
            case CpMethodType:
                entry.a = r.u2();
                break;
            case CpFieldref:
            case CpMethodref:
            case CpInterfaceMethodref:
            case CpNameAndType:
            case CpInvokeDynamic:
                entry.a = r.u2();
                entry.b = r.u2();
                break;
            case CpMethodHandle:
                entry.a = r.u1();
                entry.b = r.u2();
                break;
            default:
                throw std::runtime_error("unsupported constant pool tag " + std::to_string(entry.tag));
        }
        cls.cp[i] = entry;
    }

    cls.access = r.u2();
    cls.thisClass = className(cls.cp, r.u2());
    cls.superClass = className(cls.cp, r.u2());

    uint16_t interfaceCount = r.u2();
    for (uint16_t i = 0; i < interfaceCount; ++i) {
        cls.interfaces.push_back(className(cls.cp, r.u2()));
    }

    uint16_t fieldCount = r.u2();
    for (uint16_t i = 0; i < fieldCount; ++i) {
        cls.fields.push_back(readField(r, cls.cp));
    }

    uint16_t methodCount = r.u2();
    for (uint16_t i = 0; i < methodCount; ++i) {
        cls.methods.push_back(readMethod(r, cls.cp));
    }

    uint16_t attrCount = r.u2();
    for (uint16_t i = 0; i < attrCount; ++i) {
        std::string name = utf8(cls.cp, r.u2());
        cls.attributes.push_back(name);
        r.skip(r.u4());
    }

    return cls;
}

} // namespace jvmpoc
