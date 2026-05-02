#include "class_file.hpp"
#include "extracted_midlet.hpp"
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
    std::vector<std::string> expectedDisplayCurrents;
    std::vector<std::string> expectedRenderGraphicsOps;
    bool midlet = false;
};

class TestHost final : public jvmpoc::JvmHost {
public:
    int screenWidth() const override { return 240; }
    int screenHeight() const override { return 320; }
    uint32_t millis() const override { return 123; }
    void present(const uint16_t* pixels, int width, int height) override {
        lastPresentWidth = width;
        lastPresentHeight = height;
        if (pixels != nullptr && width > 0 && height > 0) {
            lastPixels.assign(pixels, pixels + static_cast<size_t>(width * height));
        }
        ++presentCount;
    }

    int lastPresentWidth = 0;
    int lastPresentHeight = 0;
    int presentCount = 0;
    std::vector<uint16_t> lastPixels;
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

std::vector<std::string> displayCurrents(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> currents;
    for (const jvmpoc::DisplaySetCurrent& setCurrent : trace.displaySetCurrents) {
        currents.push_back(setCurrent.display.text + ".setCurrent(" + setCurrent.displayable.text + ")");
    }
    return currents;
}

std::vector<std::string> graphicsOps(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> ops;
    for (const jvmpoc::GraphicsOp& op : trace.graphicsOps) {
        ops.push_back(op.op);
    }
    return ops;
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
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/NativeRuntime")));
    jvmpoc::appendDefaultBootClasses(classes);

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
    jvmpoc::ExecutionTrace renderTrace;
    if (test.midlet) {
        TestHost host;
        jvmpoc::JvmMidletApp app(host);
        app.setClasses(classes);
        trace = app.start(test.mainClass);
        renderTrace = app.render();
        if (host.presentCount != 1 || host.lastPresentWidth != host.screenWidth() ||
            host.lastPresentHeight != host.screenHeight()) {
            std::cout << "FAIL " << test.name << ": render did not present expected frame\n";
            return false;
        }
        if (host.lastPixels.empty() || host.lastPixels[0] != 0xffff) {
            std::cout << "FAIL " << test.name << ": render did not fill white background\n";
            return false;
        }
        bool hasBlackPixel = false;
        for (uint16_t pixel : host.lastPixels) {
            hasBlackPixel = hasBlackPixel || pixel == 0x0000;
        }
        if (!hasBlackPixel) {
            std::cout << "FAIL " << test.name << ": render did not draw black foreground pixels\n";
            return false;
        }
    } else {
        trace = jvmpoc::executeStraightLine(classes, *mainClass, *main);
    }
    bool ok = true;
    ok = expectList(test.name, "stdout", stdoutValues(trace), test.expectedStdout) && ok;
    ok = expectList(test.name, "freed objects", lastFreedObjects(trace), test.expectedFreedObjects) && ok;
    ok = expectList(test.name, "freed arrays", lastFreedArrays(trace), test.expectedFreedArrays) && ok;
    ok = expectList(test.name, "freed strings", lastFreedStrings(trace), test.expectedFreedStrings) && ok;
    ok = expectList(test.name, "unknown calls", unknownCalls(trace), test.expectedUnknownCalls) && ok;
    ok = expectList(test.name, "display currents", displayCurrents(trace), test.expectedDisplayCurrents) && ok;
    if (test.midlet) {
        ok = expectList(test.name, "render unknown calls", unknownCalls(renderTrace), test.expectedUnknownCalls) && ok;
        ok = expectList(test.name, "render graphics ops", graphicsOps(renderTrace), test.expectedRenderGraphicsOps) && ok;
    }

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
            {"obj#2"},
            {},
            {},
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
            {},
            {},
        },
        TestCase{
            "string constructors",
            "dev/roman/hello/StringConstructors",
            {"dev/roman/hello/StringConstructors"},
            {"ell", "ell", "3", "0"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot StringBuffer",
            "dev/roman/hello/StringBufferBoot",
            {"dev/roman/hello/StringBufferBoot"},
            {"enemy[2]10.png", "14", "16"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot Thread",
            "dev/roman/hello/ThreadBoot",
            {"dev/roman/hello/ThreadBoot", "dev/roman/hello/ThreadBootTask"},
            {"thread-run", "main-done"},
            {},
            {},
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
            {},
            {},
            {},
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
            {},
            {},
            {},
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
            {},
            {"display#1.setCurrent(obj#2)"},
            {
                "setColor(255,255,255)",
                "fillRect(0,0,240,320)",
                "setColor(0,0,0)",
                "drawString(\"Hello World!\",120,160,65)",
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
