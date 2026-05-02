#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Reader {
    explicit Reader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    uint8_t u1() {
        require(1);
        return bytes_[pos_++];
    }

    uint16_t u2() {
        uint16_t hi = u1();
        uint16_t lo = u1();
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    uint32_t u4() {
        uint32_t b1 = u1();
        uint32_t b2 = u1();
        uint32_t b3 = u1();
        uint32_t b4 = u1();
        return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
    }

    void skip(size_t len) {
        require(len);
        pos_ += len;
    }

    std::vector<uint8_t> bytes(size_t len) {
        require(len);
        std::vector<uint8_t> out(bytes_.begin() + static_cast<long>(pos_),
                                 bytes_.begin() + static_cast<long>(pos_ + len));
        pos_ += len;
        return out;
    }

private:
    void require(size_t len) const {
        if (pos_ + len > bytes_.size()) {
            throw std::runtime_error("unexpected end of class file");
        }
    }

    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
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

bool has(uint16_t flags, uint16_t bit) {
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

    if (has(flags, 0x0001)) add("public");
    if (has(flags, 0x0002)) add("private");
    if (has(flags, 0x0004)) add("protected");
    if (has(flags, 0x0008)) add("static");
    if (has(flags, 0x0010)) add("final");
    if (method && has(flags, 0x0020)) add("synchronized");
    if (!method && has(flags, 0x0040)) add("volatile");
    if (method && has(flags, 0x0040)) add("bridge");
    if (!method && has(flags, 0x0080)) add("transient");
    if (method && has(flags, 0x0080)) add("varargs");
    if (has(flags, 0x0400)) add("abstract");
    if (has(flags, 0x1000)) add("synthetic");

    return out.empty() ? "package" : out;
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

size_t fieldSlots(const std::string& desc) {
    size_t pos = 0;
    return typeSlotsAt(desc, pos);
}

size_t argumentSlots(const MethodInfo& method) {
    size_t slots = has(method.access, 0x0008) ? 0 : 1; // local 0 is this
    size_t pos = 0;
    if (method.descriptor.empty() || method.descriptor[pos++] != '(') {
        return slots;
    }

    while (pos < method.descriptor.size() && method.descriptor[pos] != ')') {
        slots += typeSlotsAt(method.descriptor, pos);
    }
    return slots;
}

void skipAttributes(Reader& r, const std::vector<CpEntry>& cp) {
    uint16_t count = r.u2();
    for (uint16_t i = 0; i < count; ++i) {
        (void)utf8(cp, r.u2());
        uint32_t len = r.u4();
        r.skip(len);
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
        if (attrName == "Code") {
            std::vector<uint8_t> attrBytes = r.bytes(attrLen);
            Reader code(attrBytes);
            method.hasCode = true;
            method.maxStack = code.u2();
            method.maxLocals = code.u2();
            method.codeLength = code.u4();
            code.skip(method.codeLength);

            uint16_t exceptionTableLength = code.u2();
            code.skip(static_cast<size_t>(exceptionTableLength) * 8);

            uint16_t codeAttributesCount = code.u2();
            for (uint16_t j = 0; j < codeAttributesCount; ++j) {
                std::string codeAttrName = utf8(cp, code.u2());
                uint32_t codeAttrLen = code.u4();
                if (codeAttrName == "LocalVariableTable") {
                    std::vector<uint8_t> lvtBytes = code.bytes(codeAttrLen);
                    Reader lvt(lvtBytes);
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
                } else {
                    code.skip(codeAttrLen);
                }
            }
        } else {
            r.skip(attrLen);
        }
    }

    return method;
}

ClassFile parseClass(const std::string& path) {
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
        uint32_t len = r.u4();
        cls.attributes.push_back(name);
        r.skip(len);
    }

    return cls;
}

void printClass(const ClassFile& cls) {
    std::cout << "class " << cls.thisClass << "\n";
    std::cout << "  version: " << cls.major << "." << cls.minor << "\n";
    std::cout << "  extends: " << (cls.superClass.empty() ? "<none>" : cls.superClass) << "\n";

    if (!cls.interfaces.empty()) {
        std::cout << "  interfaces:";
        for (const std::string& iface : cls.interfaces) {
            std::cout << ' ' << iface;
        }
        std::cout << "\n";
    }

    size_t objectSlots = 0;
    size_t staticSlots = 0;
    std::cout << "\n  object fields:\n";
    for (const FieldInfo& field : cls.fields) {
        if (has(field.access, 0x0008)) {
            continue;
        }
        size_t slots = fieldSlots(field.descriptor);
        std::cout << "    slot " << objectSlots << ": " << field.name << " "
                  << field.descriptor << " (" << slots << " slot"
                  << (slots == 1 ? "" : "s") << ", " << accessText(field.access, false) << ")\n";
        objectSlots += slots;
    }
    if (objectSlots == 0) {
        std::cout << "    <none>\n";
    }
    std::cout << "    total object field slots: " << objectSlots << "\n";

    std::cout << "\n  static fields:\n";
    for (const FieldInfo& field : cls.fields) {
        if (!has(field.access, 0x0008)) {
            continue;
        }
        size_t slots = fieldSlots(field.descriptor);
        std::cout << "    slot " << staticSlots << ": " << field.name << " "
                  << field.descriptor << " (" << slots << " slot"
                  << (slots == 1 ? "" : "s") << ", " << accessText(field.access, false) << ")\n";
        staticSlots += slots;
    }
    if (staticSlots == 0) {
        std::cout << "    <none>\n";
    }
    std::cout << "    total static field slots: " << staticSlots << "\n";

    std::cout << "\n  methods:\n";
    for (const MethodInfo& method : cls.methods) {
        std::cout << "    " << method.name << method.descriptor << " [" << accessText(method.access, true) << "]\n";
        std::cout << "      args/local inputs: " << argumentSlots(method) << " slot"
                  << (argumentSlots(method) == 1 ? "" : "s") << "\n";
        if (method.hasCode) {
            std::cout << "      frame: max_locals=" << method.maxLocals
                      << ", max_stack=" << method.maxStack
                      << ", bytecode=" << method.codeLength << " bytes\n";
            if (!method.locals.empty()) {
                std::cout << "      local table:\n";
                for (const MethodInfo::LocalVariable& local : method.locals) {
                    std::cout << "        [" << local.index << "] " << local.name
                              << " " << local.descriptor
                              << " pc=" << local.startPc << ".."
                              << (local.startPc + local.length) << "\n";
                }
            }
        } else {
            std::cout << "      frame: <native/abstract/no Code attribute>\n";
        }
    }

    if (!cls.attributes.empty()) {
        std::cout << "\n  class attributes:";
        for (const std::string& attr : cls.attributes) {
            std::cout << ' ' << attr;
        }
        std::cout << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <class-file> [class-file...]\n";
        return 2;
    }

    try {
        for (int i = 1; i < argc; ++i) {
            if (i > 1) {
                std::cout << "\n";
            }
            printClass(parseClass(argv[i]));
        }
    } catch (const std::exception& e) {
        std::cerr << "classdump: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
