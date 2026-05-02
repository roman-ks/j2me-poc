#include "class_file.hpp"
#include "interpreter.hpp"
#include "jvm_midlet_app.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct TestCase {
    std::string name;
    std::string mainClass;
    std::vector<std::string> classFiles;
    std::vector<std::string> expectedStdout;
    std::vector<std::string> expectedFreedObjects;
    std::vector<std::string> expectedFreedArrays;
    std::vector<std::string> expectedFreedStrings;
    std::vector<std::string> expectedUnknownCalls;
    bool midlet = false;
};

class TestHost final : public jvmpoc::JvmHost {
public:
    int screenWidth() const override { return 240; }
    int screenHeight() const override { return 320; }
    uint32_t millis() const override { return 123; }
    void present(const uint16_t* /*pixels*/, int width, int height) override {
        lastPresentWidth = width;
        lastPresentHeight = height;
        ++presentCount;
    }

    int lastPresentWidth = 0;
    int lastPresentHeight = 0;
    int presentCount = 0;
};

std::string classPath(const std::string& root, const std::string& className) {
    return root + "/target/classes/" + className + ".class";
}

const jvmpoc::ClassFile* findClass(const std::vector<jvmpoc::ClassFile>& classes, const std::string& name) {
    for (const jvmpoc::ClassFile& cls : classes) {
        if (cls.thisClass == name) {
            return &cls;
        }
    }
    return nullptr;
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

std::vector<std::string> stdoutValues(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> values;
    for (const jvmpoc::RuntimePrint& print : trace.runtimePrints) {
        values.push_back(print.value.text);
    }
    return values;
}

std::vector<std::string> unknownCalls(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> calls;
    for (const jvmpoc::UnknownMethodCall& call : trace.unknownMethodCalls) {
        calls.push_back(call.methodName);
    }
    return calls;
}

std::vector<std::string> valueTexts(const std::vector<jvmpoc::Value>& values) {
    std::vector<std::string> texts;
    for (const jvmpoc::Value& value : values) {
        texts.push_back(value.text);
    }
    return texts;
}

std::vector<std::string> lastFreedObjects(const jvmpoc::ExecutionTrace& trace) {
    if (trace.gcReports.empty()) {
        return {};
    }
    return valueTexts(trace.gcReports.back().freedObjects);
}

std::vector<std::string> lastFreedArrays(const jvmpoc::ExecutionTrace& trace) {
    if (trace.gcReports.empty()) {
        return {};
    }
    return valueTexts(trace.gcReports.back().freedArrays);
}

std::vector<std::string> lastFreedStrings(const jvmpoc::ExecutionTrace& trace) {
    if (trace.gcReports.empty()) {
        return {};
    }
    return valueTexts(trace.gcReports.back().freedStrings);
}

void printList(const std::vector<std::string>& values) {
    std::cout << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            std::cout << ", ";
        }
        std::cout << values[i];
    }
    std::cout << "]";
}

bool expectList(const std::string& testName, const std::string& field, const std::vector<std::string>& actual, const std::vector<std::string>& expected) {
    if (actual == expected) {
        return true;
    }

    std::cout << "FAIL " << testName << ": " << field << "\n  expected ";
    printList(expected);
    std::cout << "\n  actual   ";
    printList(actual);
    std::cout << "\n";
    return false;
}

bool runCase(const std::string& root, const TestCase& test) {
    std::vector<jvmpoc::ClassFile> classes;
    for (const std::string& classFile : test.classFiles) {
        classes.push_back(jvmpoc::parseClassFile(classPath(root, classFile)));
    }

    const jvmpoc::ClassFile* mainClass = findClass(classes, test.mainClass);
    if (mainClass == nullptr) {
        std::cout << "FAIL " << test.name << ": main class not loaded\n";
        return false;
    }

    const jvmpoc::MethodInfo* main = test.midlet ? nullptr : findMain(*mainClass);
    if (!test.midlet && main == nullptr) {
        std::cout << "FAIL " << test.name << ": main method not found\n";
        return false;
    }

    jvmpoc::ExecutionTrace trace;
    if (test.midlet) {
        TestHost host;
        jvmpoc::JvmMidletApp app(host);
        app.setClasses(classes);
        trace = app.start(test.mainClass);
    } else {
        trace = jvmpoc::executeStraightLine(classes, *mainClass, *main);
    }
    bool ok = true;
    ok = expectList(test.name, "stdout", stdoutValues(trace), test.expectedStdout) && ok;
    ok = expectList(test.name, "freed objects", lastFreedObjects(trace), test.expectedFreedObjects) && ok;
    ok = expectList(test.name, "freed arrays", lastFreedArrays(trace), test.expectedFreedArrays) && ok;
    ok = expectList(test.name, "freed strings", lastFreedStrings(trace), test.expectedFreedStrings) && ok;
    ok = expectList(test.name, "unknown calls", unknownCalls(trace), test.expectedUnknownCalls) && ok;

    if (ok) {
        std::cout << "PASS " << test.name << "\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : ".";
    std::vector<TestCase> tests = {
        TestCase{
            "int arrays",
            "dev/roman/hello/IntArrays",
            {"dev/roman/hello/IntArrays"},
            {"18"},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "strings",
            "dev/roman/hello/Strings",
            {"dev/roman/hello/Strings"},
            {"hello", "literal"},
            {},
            {},
            {"str#2"},
            {},
        },
        TestCase{
            "string length",
            "dev/roman/hello/StringLength",
            {"dev/roman/hello/StringLength"},
            {"3", "5"},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "gc roots",
            "dev/roman/hello/GcRoots",
            {"dev/roman/hello/Objects", "dev/roman/hello/GcRoots"},
            {"7", "12", "9"},
            {"obj#2"},
            {"arr#1"},
            {},
            {
                "java/lang/Object.<init>()V",
                "java/lang/Object.<init>()V",
                "java/lang/Object.<init>()V",
            },
        },
        TestCase{
            "inherited method lookup",
            "dev/roman/hello/Inheritance",
            {
                "dev/roman/hello/InheritanceBase",
                "dev/roman/hello/InheritanceChild",
                "dev/roman/hello/Inheritance",
            },
            {"42"},
            {},
            {},
            {},
            {"java/lang/Object.<init>()V"},
        },
        TestCase{
            "midlet lifecycle unknowns",
            "dev/roman/j2mepoc/HelloMidletMini",
            {
                "dev/roman/j2mepoc/HelloMidletMini",
                "dev/roman/j2mepoc/HelloMidletMini$MyCanvas",
            },
            {},
            {},
            {},
            {},
            {
                "javax/microedition/midlet/MIDlet.<init>()V",
                "javax/microedition/lcdui/Canvas.<init>()V",
                "javax/microedition/lcdui/Display.getDisplay(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;",
                "javax/microedition/lcdui/Display.setCurrent(Ljavax/microedition/lcdui/Displayable;)V",
            },
            true,
        },
    };

    try {
        size_t failed = 0;
        for (const TestCase& test : tests) {
            if (!runCase(root, test)) {
                ++failed;
            }
        }

        if (failed != 0) {
            std::cout << failed << " test(s) failed\n";
            return 1;
        }
        std::cout << "All jvm-poc C++ tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "jvm-poc-tests: " << e.what() << "\n";
        return 1;
    }
}
