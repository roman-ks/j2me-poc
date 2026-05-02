#include "class_file.hpp"
#include "interpreter.hpp"

#include <exception>
#include <iostream>
#include <vector>

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

    }

    if (!cls.attributes.empty()) {
        std::cout << "\n  class attributes:";
        for (const std::string& attr : cls.attributes) {
            std::cout << ' ' << attr;
        }
        std::cout << "\n";
    }
}

const jvmpoc::MethodInfo* findMain(const jvmpoc::ClassFile& cls) {
    for (const jvmpoc::MethodInfo& method : cls.methods) {
        if (method.name == "main" && method.descriptor == "([Ljava/lang/String;)V" &&
            jvmpoc::hasAccess(method.access, 0x0008)) {
            return &method;
        }
    }
    return nullptr;
}

void printRuntimeTrace(const std::vector<jvmpoc::ClassFile>& classes, const jvmpoc::ClassFile& cls, const jvmpoc::MethodInfo& method) {
    std::cout << "runtime " << cls.thisClass << "." << method.name << method.descriptor << "\n";
    jvmpoc::ExecutionTrace trace = jvmpoc::executeStraightLine(classes, cls, method);

    if (!trace.localWrites.empty()) {
        std::cout << "  local writes:\n";
        for (const jvmpoc::LocalWrite& write : trace.localWrites) {
            std::cout << "    " << write.methodLabel << " pc=" << write.pc << " ["
                      << write.index << "] "
                      << (write.localName.empty() ? "local" + std::to_string(write.index) : write.localName)
                      << " = " << write.value.text;
            if (!write.reason.empty() && write.reason != "store") {
                std::cout << " (" << write.reason << ")";
            }
            std::cout << "\n";
        }
    }

    if (!trace.branches.empty()) {
        std::cout << "  branches:\n";
        for (const jvmpoc::BranchTrace& branch : trace.branches) {
            std::cout << "    " << branch.methodLabel << " pc=" << branch.pc << " if " << branch.condition
                      << " -> " << (branch.known ? (branch.taken ? "taken" : "not taken") : "unknown")
                      << " target=" << branch.targetPc << "\n";
        }
    }

    if (!trace.staticWrites.empty()) {
        std::cout << "  static writes:\n";
        for (const jvmpoc::StaticWrite& write : trace.staticWrites) {
            std::cout << "    " << write.methodLabel << " pc=" << write.pc
                      << " " << write.fieldName << " = " << write.value.text << "\n";
        }
    }

    if (!trace.objectAllocs.empty()) {
        std::cout << "  object allocs:\n";
        for (const jvmpoc::ObjectAlloc& alloc : trace.objectAllocs) {
            std::cout << "    " << alloc.methodLabel << " pc=" << alloc.pc
                      << " " << alloc.ref.text << " = new " << alloc.className << "\n";
        }
    }

    if (!trace.fieldWrites.empty()) {
        std::cout << "  field writes:\n";
        for (const jvmpoc::FieldWrite& write : trace.fieldWrites) {
            std::cout << "    " << write.methodLabel << " pc=" << write.pc
                      << " " << write.ref.text << "." << write.fieldName
                      << " = " << write.value.text << "\n";
        }
    }

    if (!trace.arrayAllocs.empty()) {
        std::cout << "  array allocs:\n";
        for (const jvmpoc::ArrayAlloc& alloc : trace.arrayAllocs) {
            std::cout << "    " << alloc.methodLabel << " pc=" << alloc.pc
                      << " " << alloc.ref.text << " = new int[" << alloc.length << "]\n";
        }
    }

    if (!trace.arrayWrites.empty()) {
        std::cout << "  array writes:\n";
        for (const jvmpoc::ArrayWrite& write : trace.arrayWrites) {
            std::cout << "    " << write.methodLabel << " pc=" << write.pc
                      << " " << write.ref.text << "[" << write.index.text << "]"
                      << " = " << write.value.text << "\n";
        }
    }

    if (!trace.runtimePrints.empty()) {
        std::cout << "  stdout:\n";
        for (const jvmpoc::RuntimePrint& print : trace.runtimePrints) {
            std::cout << "    " << print.methodLabel << " pc=" << print.pc << ": " << print.value.text << "\n";
        }
    }

    if (trace.localWrites.empty() && trace.branches.empty() && trace.staticWrites.empty() &&
        trace.objectAllocs.empty() && trace.fieldWrites.empty() &&
        trace.arrayAllocs.empty() && trace.arrayWrites.empty() && trace.runtimePrints.empty()) {
        std::cout << "  <no observable toy-runtime effects>\n";
    }
    if (trace.stepLimitHit) {
        std::cout << "  stopped: execution step limit hit\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <class-file> [class-file...]\n";
        return 2;
    }

    try {
        std::vector<jvmpoc::ClassFile> classes;
        for (int i = 1; i < argc; ++i) {
            classes.push_back(jvmpoc::parseClassFile(argv[i]));
        }

        std::cout << "metadata\n";
        for (int i = 1; i < argc; ++i) {
            if (i > 1) {
                std::cout << "\n";
            }
            printClass(classes[static_cast<size_t>(i - 1)]);
        }

        bool ran = false;
        for (const jvmpoc::ClassFile& cls : classes) {
            const jvmpoc::MethodInfo* mainMethod = findMain(cls);
            if (mainMethod == nullptr) {
                continue;
            }
            std::cout << "\n";
            if (!ran) {
                std::cout << "runtime\n";
            }
            printRuntimeTrace(classes, cls, *mainMethod);
            ran = true;
        }

        if (!ran) {
            std::cout << "\nruntime\n  <no public static main([Ljava/lang/String;)V found>\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "jvm-poc: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
