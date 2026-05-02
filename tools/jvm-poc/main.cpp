#include "class_file.hpp"
#include "interpreter.hpp"
#include "jvm_midlet_app.hpp"

#include <exception>
#include <iostream>
#include <string>
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

void printTraceBody(const jvmpoc::ExecutionTrace& trace) {
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

    if (!trace.displaySetCurrents.empty()) {
        std::cout << "  display:\n";
        for (const jvmpoc::DisplaySetCurrent& setCurrent : trace.displaySetCurrents) {
            std::cout << "    " << setCurrent.methodLabel << " pc=" << setCurrent.pc
                      << " " << setCurrent.display.text << ".setCurrent("
                      << setCurrent.displayable.text << ")\n";
        }
    }

    if (!trace.canvasSizeQueries.empty()) {
        std::cout << "  canvas queries:\n";
        for (const jvmpoc::CanvasSizeQuery& query : trace.canvasSizeQueries) {
            std::cout << "    " << query.methodLabel << " pc=" << query.pc
                      << " " << query.canvas.text << "." << query.methodName
                      << " -> " << query.value << "\n";
        }
    }

    if (!trace.graphicsOps.empty()) {
        std::cout << "  graphics:\n";
        for (const jvmpoc::GraphicsOp& op : trace.graphicsOps) {
            std::cout << "    " << op.methodLabel << " pc=" << op.pc
                      << " " << op.op << "\n";
        }
    }

    if (!trace.imageLoads.empty()) {
        std::cout << "  images:\n";
        for (const jvmpoc::ImageLoad& load : trace.imageLoads) {
            std::cout << "    " << load.methodLabel << " pc=" << load.pc
                      << " " << load.image.text << " = " << load.source
                      << " (" << load.width << "x" << load.height << ")\n";
        }
    }

    if (!trace.unsupportedStringCalls.empty()) {
        std::cout << "  unsupported string calls:\n";
        for (const jvmpoc::UnsupportedStringCall& call : trace.unsupportedStringCalls) {
            std::cout << "    " << call.methodLabel << " pc=" << call.pc
                      << " " << call.receiver.text << "." << call.methodName << "\n";
        }
    }

    if (!trace.unknownMethodCalls.empty()) {
        std::cout << "  unknown method calls:\n";
        for (const jvmpoc::UnknownMethodCall& call : trace.unknownMethodCalls) {
            std::cout << "    " << call.methodLabel << " pc=" << call.pc
                      << " " << call.methodName;
            if (call.nooped) {
                std::cout << " noop";
            }
            if (!call.result.empty()) {
                std::cout << " -> " << call.result;
            }
            std::cout << "\n";
        }
    }

    if (!trace.gcReports.empty()) {
        std::cout << "  gc reports:\n";
        for (const jvmpoc::GcReport& report : trace.gcReports) {
            std::cout << "    " << report.when << "\n";
            if (!report.roots.empty()) {
                std::cout << "      roots:";
                for (const std::string& root : report.roots) {
                    std::cout << " " << root;
                }
                std::cout << "\n";
            }
            if (!report.unreachableObjects.empty()) {
                std::cout << "      unreachable objects:";
                for (const jvmpoc::Value& value : report.unreachableObjects) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (!report.unreachableArrays.empty()) {
                std::cout << "      unreachable arrays:";
                for (const jvmpoc::Value& value : report.unreachableArrays) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (!report.unreachableStrings.empty()) {
                std::cout << "      unreachable strings:";
                for (const jvmpoc::Value& value : report.unreachableStrings) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (!report.freedObjects.empty()) {
                std::cout << "      freed objects:";
                for (const jvmpoc::Value& value : report.freedObjects) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (!report.freedArrays.empty()) {
                std::cout << "      freed arrays:";
                for (const jvmpoc::Value& value : report.freedArrays) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (!report.freedStrings.empty()) {
                std::cout << "      freed strings:";
                for (const jvmpoc::Value& value : report.freedStrings) {
                    std::cout << " " << value.text;
                }
                std::cout << "\n";
            }
            if (report.roots.empty() && report.unreachableObjects.empty() && report.unreachableArrays.empty() &&
                report.unreachableStrings.empty() && report.freedObjects.empty() && report.freedArrays.empty() &&
                report.freedStrings.empty()) {
                std::cout << "      <nothing to collect>\n";
            }
        }
    }

    if (trace.localWrites.empty() && trace.branches.empty() && trace.staticWrites.empty() &&
        trace.objectAllocs.empty() && trace.fieldWrites.empty() &&
        trace.arrayAllocs.empty() && trace.arrayWrites.empty() && trace.runtimePrints.empty() &&
        trace.unsupportedStringCalls.empty() && trace.unknownMethodCalls.empty() && trace.gcReports.empty() &&
        trace.imageLoads.empty() && trace.graphicsOps.empty()) {
        std::cout << "  <no observable toy-runtime effects>\n";
    }
    if (trace.stepLimitHit) {
        std::cout << "  stopped: execution step limit hit\n";
    }
}

void printRuntimeTrace(const std::vector<jvmpoc::ClassFile>& classes, const jvmpoc::ClassFile& cls, const jvmpoc::MethodInfo& method) {
    std::cout << "runtime " << cls.thisClass << "." << method.name << method.descriptor << "\n";
    printTraceBody(jvmpoc::executeStraightLine(classes, cls, method));
}

void printMidletTrace(const std::vector<jvmpoc::ClassFile>& classes, const std::string& className) {
    std::cout << "runtime midlet " << className << "\n";
    jvmpoc::NullJvmHost host;
    jvmpoc::JvmMidletApp app(host);
    app.setClasses(classes);
    printTraceBody(app.start(className));
}

void printStdoutOnly(const jvmpoc::ExecutionTrace& trace) {
    for (const jvmpoc::RuntimePrint& print : trace.runtimePrints) {
        std::cout << print.value.text << "\n";
    }
}

void printUsage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " [--metadata] [--stdout-only] [--midlet class/name] <class-file> [class-file...]\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 2;
    }

    try {
        bool showMetadata = false;
        bool stdoutOnly = false;
        std::string midletClass;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--metadata") {
                showMetadata = true;
            } else if (arg == "--stdout-only") {
                stdoutOnly = true;
            } else if (arg == "--midlet") {
                if (i + 1 >= argc) {
                    printUsage(argv[0]);
                    return 2;
                }
                midletClass = argv[++i];
            } else if (arg == "--help" || arg == "-h") {
                printUsage(argv[0]);
                return 0;
            } else {
                paths.push_back(arg);
            }
        }
        if (paths.empty()) {
            printUsage(argv[0]);
            return 2;
        }

        std::vector<jvmpoc::ClassFile> classes;
        for (const std::string& path : paths) {
            classes.push_back(jvmpoc::parseClassFile(path));
        }

        if (showMetadata && !stdoutOnly) {
            std::cout << "metadata\n";
            for (size_t i = 0; i < classes.size(); ++i) {
                if (i > 0) {
                    std::cout << "\n";
                }
                printClass(classes[i]);
            }
        }

        bool ran = false;
        if (!midletClass.empty()) {
            if (stdoutOnly) {
                jvmpoc::NullJvmHost host;
                jvmpoc::JvmMidletApp app(host);
                app.setClasses(classes);
                printStdoutOnly(app.start(midletClass));
            } else {
                if (showMetadata) {
                    std::cout << "\n";
                }
                std::cout << "runtime\n";
                printMidletTrace(classes, midletClass);
            }
            ran = true;
        } else {
            for (const jvmpoc::ClassFile& cls : classes) {
                const jvmpoc::MethodInfo* mainMethod = findMain(cls);
                if (mainMethod == nullptr) {
                    continue;
                }
                if (stdoutOnly) {
                    printStdoutOnly(jvmpoc::executeStraightLine(classes, cls, *mainMethod));
                } else {
                    if (showMetadata || ran) {
                        std::cout << "\n";
                    }
                    if (!ran) {
                        std::cout << "runtime\n";
                    }
                    printRuntimeTrace(classes, cls, *mainMethod);
                }
                ran = true;
            }
        }

        if (!ran && !stdoutOnly) {
            if (showMetadata) {
                std::cout << "\n";
            }
            std::cout << "\nruntime\n  <no public static main([Ljava/lang/String;)V found>\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "jvm-poc: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
