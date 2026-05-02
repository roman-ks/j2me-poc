#include "class_file.hpp"
#include "interpreter.hpp"

#include <exception>
#include <iostream>

namespace {

void printClass(const jvmpoc::ClassFile& cls) {
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
    for (const jvmpoc::FieldInfo& field : cls.fields) {
        if (jvmpoc::hasAccess(field.access, 0x0008)) {
            continue;
        }
        size_t slots = jvmpoc::fieldSlots(field.descriptor);
        std::cout << "    slot " << objectSlots << ": " << field.name << " "
                  << field.descriptor << " (" << slots << " slot"
                  << (slots == 1 ? "" : "s") << ", " << jvmpoc::accessText(field.access, false) << ")\n";
        objectSlots += slots;
    }
    if (objectSlots == 0) {
        std::cout << "    <none>\n";
    }
    std::cout << "    total object field slots: " << objectSlots << "\n";

    std::cout << "\n  static fields:\n";
    for (const jvmpoc::FieldInfo& field : cls.fields) {
        if (!jvmpoc::hasAccess(field.access, 0x0008)) {
            continue;
        }
        size_t slots = jvmpoc::fieldSlots(field.descriptor);
        std::cout << "    slot " << staticSlots << ": " << field.name << " "
                  << field.descriptor << " (" << slots << " slot"
                  << (slots == 1 ? "" : "s") << ", " << jvmpoc::accessText(field.access, false) << ")\n";
        staticSlots += slots;
    }
    if (staticSlots == 0) {
        std::cout << "    <none>\n";
    }
    std::cout << "    total static field slots: " << staticSlots << "\n";

    std::cout << "\n  methods:\n";
    for (const jvmpoc::MethodInfo& method : cls.methods) {
        std::cout << "    " << method.name << method.descriptor << " ["
                  << jvmpoc::accessText(method.access, true) << "]\n";
        std::cout << "      args/local inputs: " << jvmpoc::argumentSlots(method) << " slot"
                  << (jvmpoc::argumentSlots(method) == 1 ? "" : "s") << "\n";
        if (!method.hasCode) {
            std::cout << "      frame: <native/abstract/no Code attribute>\n";
            continue;
        }

        std::cout << "      frame: max_locals=" << method.maxLocals
                  << ", max_stack=" << method.maxStack
                  << ", bytecode=" << method.codeLength << " bytes\n";
        if (!method.locals.empty()) {
            std::cout << "      local table:\n";
            for (const jvmpoc::MethodInfo::LocalVariable& local : method.locals) {
                std::cout << "        [" << local.index << "] " << local.name
                          << " " << local.descriptor
                          << " pc=" << local.startPc << ".."
                          << (local.startPc + local.length) << "\n";
            }
        }

        std::vector<jvmpoc::LocalWrite> writes = jvmpoc::inferStraightLineLocalWrites(method);
        if (!writes.empty()) {
            std::cout << "      inferred local writes:\n";
            for (const jvmpoc::LocalWrite& write : writes) {
                std::cout << "        pc=" << write.pc << " ["
                          << write.index << "] "
                          << jvmpoc::localNameAt(method, write.index, write.pc)
                          << " = " << write.value.text << "\n";
            }
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
            printClass(jvmpoc::parseClassFile(argv[i]));
        }
    } catch (const std::exception& e) {
        std::cerr << "jvm-poc: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
