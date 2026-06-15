#include "class_file.hpp"
#include "extracted_midlet.hpp"
#include "interpreter.hpp"
#include "j2me_port/J2MECompat.hpp"
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
    std::vector<std::pair<size_t, uint16_t>> expectedPixels = {};
    std::vector<std::string> expectedUncaughtExceptions = {};
    std::vector<std::string> expectedThreadDeaths = {};
    // Default: midlet tests expect render() to present a frame. Set false
    // for midlets that don't set a displayable (e.g. a task throws before
    // any UI is shown) — render returns early without calling host.present
    // and the present-count check would otherwise fail.
    bool expectsPresent = true;
};

class TestHost final : public jvmpoc::JvmHost {
public:
    int screenWidth() const override { return 240; }
    int screenHeight() const override { return 320; }
    uint32_t millis() const override { return nowMillis; }
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
    uint32_t nowMillis = 123;
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

// asText() prefixes kLong/kStr with "long#"/"str#" so debug traces can
// distinguish them from kInt/handles. Game stdout and trace records that
// echo user-facing values want the plain rendering instead.
std::string displayText(const jvmpoc::Value& value) {
    std::string s = value.asText();
    if (s.rfind("long#", 0) == 0) s.erase(0, 5);
    else if (s.rfind("str#", 0) == 0) s.erase(0, 4);
    return s;
}

std::vector<std::string> stdoutValues(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> values;
    for (const jvmpoc::RuntimePrint& print : trace.runtimePrints) {
        values.push_back(displayText(print.value));
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
        currents.push_back(displayText(setCurrent.display) + ".setCurrent(" + displayText(setCurrent.displayable) + ")");
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

std::vector<std::string> uncaughtExceptions(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> exceptions;
    for (const jvmpoc::UncaughtExceptionTrace& exception : trace.uncaughtExceptions) {
        exceptions.push_back(exception.threadLabel + ":" + exception.exceptionClass);
    }
    return exceptions;
}

std::vector<std::string> threadDeaths(const jvmpoc::ExecutionTrace& trace) {
    std::vector<std::string> deaths;
    for (const jvmpoc::ThreadDeathTrace& death : trace.threadDeaths) {
        deaths.push_back(death.threadLabel + ":" + death.exceptionClass);
    }
    return deaths;
}

std::vector<std::string> appendAll(
    const std::vector<std::string>& first,
    const std::vector<std::string>& second,
    const std::vector<std::string>& third) {
    std::vector<std::string> values = first;
    values.insert(values.end(), second.begin(), second.end());
    values.insert(values.end(), third.begin(), third.end());
    return values;
}

std::vector<std::string> valueTexts(const std::vector<jvmpoc::Value>& values) {
    std::vector<std::string> texts;
    for (const jvmpoc::Value& value : values) {
        texts.push_back(displayText(value));
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
    port::setResourceRoot(root + "/target/classes");

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
    jvmpoc::ExecutionTrace pressTrace;
    jvmpoc::ExecutionTrace releaseTrace;
    std::vector<uint16_t> renderedPixels;
    const bool inputMidletTest = test.name == "canvas key events";
    const bool explicitPixelTest = !test.expectedPixels.empty();
    if (test.midlet) {
        TestHost host;
        jvmpoc::JvmMidletApp app(host);
        app.setClasses(classes);
        trace = app.start(test.mainClass);
        renderTrace = app.render();
        if (inputMidletTest) {
            host.handlePress(-3);
            pressTrace = app.render();
            host.handleRelease(-3);
            releaseTrace = app.render();
        }
        const int expectedPresentCount = !test.expectsPresent ? 0 : (inputMidletTest ? 3 : 1);
        if (host.presentCount != expectedPresentCount ||
            (expectedPresentCount > 0 && (host.lastPresentWidth != host.screenWidth() ||
                                           host.lastPresentHeight != host.screenHeight()))) {
            std::cout << "FAIL " << test.name << ": render did not present expected frame\n";
            return false;
        }
        if (test.expectsPresent && !inputMidletTest && !explicitPixelTest && (host.lastPixels.empty() || host.lastPixels[0] != 0xffff)) {
            std::cout << "FAIL " << test.name << ": render did not fill white background\n";
            return false;
        }
        bool hasBlackPixel = false;
        for (uint16_t pixel : host.lastPixels) {
            hasBlackPixel = hasBlackPixel || pixel == 0x0000;
        }
        if (test.expectsPresent && !inputMidletTest && !explicitPixelTest && !hasBlackPixel) {
            std::cout << "FAIL " << test.name << ": render did not draw black foreground pixels\n";
            return false;
        }
        renderedPixels = host.lastPixels;
    } else {
        trace = jvmpoc::executeStraightLine(classes, *mainClass, *main);
    }
    bool ok = true;
    if (inputMidletTest) {
        ok = expectList(
                 test.name,
                 "stdout",
                 appendAll(stdoutValues(trace), stdoutValues(pressTrace), stdoutValues(releaseTrace)),
                 test.expectedStdout) && ok;
    } else if (test.midlet) {
        ok = expectList(
                 test.name,
                 "stdout",
                 appendAll(stdoutValues(trace), stdoutValues(renderTrace), {}),
                 test.expectedStdout) && ok;
    } else {
        ok = expectList(test.name, "stdout", stdoutValues(trace), test.expectedStdout) && ok;
    }
    ok = expectList(test.name, "freed objects", lastFreedObjects(trace), test.expectedFreedObjects) && ok;
    ok = expectList(test.name, "freed arrays", lastFreedArrays(trace), test.expectedFreedArrays) && ok;
    ok = expectList(test.name, "freed strings", lastFreedStrings(trace), test.expectedFreedStrings) && ok;
    ok = expectList(test.name, "unknown calls", unknownCalls(trace), test.expectedUnknownCalls) && ok;
    ok = expectList(test.name, "display currents", displayCurrents(trace), test.expectedDisplayCurrents) && ok;
    std::vector<std::string> allUncaught = appendAll(
        uncaughtExceptions(trace),
        uncaughtExceptions(renderTrace),
        appendAll(uncaughtExceptions(pressTrace), uncaughtExceptions(releaseTrace), {}));
    std::vector<std::string> allThreadDeaths = appendAll(
        threadDeaths(trace),
        threadDeaths(renderTrace),
        appendAll(threadDeaths(pressTrace), threadDeaths(releaseTrace), {}));
    ok = expectList(test.name, "uncaught exceptions", allUncaught, test.expectedUncaughtExceptions) && ok;
    ok = expectList(test.name, "thread deaths", allThreadDeaths, test.expectedThreadDeaths) && ok;
    if (test.midlet) {
        ok = expectList(test.name, "render unknown calls", unknownCalls(renderTrace), test.expectedUnknownCalls) && ok;
        ok = expectList(test.name, "render graphics ops", graphicsOps(renderTrace), test.expectedRenderGraphicsOps) && ok;
        for (const auto& expectedPixel : test.expectedPixels) {
            if (expectedPixel.first >= renderedPixels.size() || renderedPixels[expectedPixel.first] != expectedPixel.second) {
                std::cout << "FAIL " << test.name << ": pixel[" << expectedPixel.first << "]\n"
                          << "  expected " << expectedPixel.second << "\n"
                          << "  actual   "
                          << (expectedPixel.first < renderedPixels.size() ? std::to_string(renderedPixels[expectedPixel.first]) : std::string("<out-of-range>"))
                          << "\n";
                ok = false;
            }
        }
    }

    if (ok) {
        std::cout << "PASS " << test.name << "\n";
    }
    return ok;
}

bool runStepLimitPreemptionCase(const std::string& root) {
    const std::string testName = "step limit preempts task";
    port::setResourceRoot(root + "/target/classes");

    std::vector<jvmpoc::ClassFile> classes;
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/StepLimitMidlet")));
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/StepLimitTask")));
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/StepLimitCanvas")));
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/NativeRuntime")));
    jvmpoc::appendDefaultBootClasses(classes);

    TestHost host;
    jvmpoc::JvmMidletApp app(host);
    app.setClasses(classes);
    (void)app.start("dev/roman/hello/StepLimitMidlet");

    jvmpoc::ExecutionTrace first = app.render();
    host.nowMillis += 2;
    jvmpoc::ExecutionTrace second = app.render();

    bool ok = true;
    if (!first.stepLimitHit || !second.stepLimitHit) {
        std::cout << "FAIL " << testName << ": expected both render passes to hit step limit\n";
        ok = false;
    }
    if (first.suspendedTasks.empty() || second.suspendedTasks.empty()) {
        std::cout << "FAIL " << testName << ": expected spinning task to remain suspended\n";
        ok = false;
    }
    if (!first.threadDeaths.empty() || !second.threadDeaths.empty()) {
        std::cout << "FAIL " << testName << ": spinning task was marked dead\n";
        ok = false;
    }
    if (ok) {
        std::cout << "PASS " << testName << "\n";
    }
    return ok;
}

bool runNestedTaskSleepCase(const std::string& root) {
    const std::string testName = "nested task sleep resumes stack";
    port::setResourceRoot(root + "/target/classes");

    std::vector<jvmpoc::ClassFile> classes;
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/NestedSleepMidlet")));
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/NestedSleepTask")));
    classes.push_back(jvmpoc::parseClassFile(classPath(root, "dev/roman/hello/NativeRuntime")));
    jvmpoc::appendDefaultBootClasses(classes);

    TestHost host;
    jvmpoc::JvmMidletApp app(host);
    app.setClasses(classes);
    (void)app.start("dev/roman/hello/NestedSleepMidlet");

    jvmpoc::ExecutionTrace first = app.render();
    host.nowMillis += 2;
    jvmpoc::ExecutionTrace second = app.render();

    const std::vector<std::string> stdout = appendAll(stdoutValues(first), stdoutValues(second), {});
    const bool ok = expectList(testName, "stdout", stdout, {"before", "after"});
    if (ok) {
        std::cout << "PASS " << testName << "\n";
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
            "resource bytes",
            "dev/roman/hello/ResourceBytes",
            {"dev/roman/hello/ResourceBytes"},
            {"4", "65", "23", "68", "tile"},
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
            {"enemy[2]10.pngtrue", "18", "34"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot String.valueOf",
            "dev/roman/hello/StringValueOf",
            {"dev/roman/hello/StringValueOf"},
            {"0", "/1000.map"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot String digits",
            "dev/roman/hello/StringDigits",
            {"dev/roman/hello/StringDigits"},
            {"2", "1", "sn1"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot String substring",
            "dev/roman/hello/StringSubstring",
            {"dev/roman/hello/StringSubstring"},
            {"alpha", "beta", "gamma"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot String bytes",
            "dev/roman/hello/StringBytes",
            {"dev/roman/hello/StringBytes"},
            {"alpha|beta", "beta"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot String indexOf",
            "dev/roman/hello/StringIndexOf",
            {"dev/roman/hello/StringIndexOf"},
            {"3", "16", "-1", "16", "-1", "87", "0", "-1", "1"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "image cache",
            "dev/roman/hello/ImageCache",
            {"dev/roman/hello/ImageCache"},
            {"1"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot Integer.toString",
            "dev/roman/hello/IntegerToString",
            {"dev/roman/hello/IntegerToString"},
            {"1[0].en", "3.sn"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot Random",
            "dev/roman/hello/RandomBoot",
            {"dev/roman/hello/RandomBoot"},
            {"901204", "65787911"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "stack ops",
            "dev/roman/hello/StackOps",
            {"dev/roman/hello/StackOps"},
            {"10", "11", "10", "7", "7", "5", "6"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "switch bytecodes",
            "dev/roman/hello/SwitchBoot",
            {"dev/roman/hello/SwitchBoot"},
            {"7", "9", "11", "1", "2", "4"},
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
            "boot DataInputStream",
            "dev/roman/hello/DataInputStreamBoot",
            {"dev/roman/hello/DataInputStreamBoot"},
            {"16909060", "5", "test", "98", "eof"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "boot RecordStore",
            "dev/roman/hello/RecordStoreBoot",
            {"dev/roman/hello/RecordStoreBoot"},
            {"1", "4", "3", "98", "deleted"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "exception handling",
            "dev/roman/hello/ExceptionHandling",
            {
                "dev/roman/hello/ExceptionHandling",
                "dev/roman/hello/MarkerException",
                "dev/roman/hello/ChildMarkerException",
            },
            {"exact", "parent", "miss-parent", "propagated", "finally-return", "3", "finally-throw", "finally-caught", "4"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "uncaught exception",
            "dev/roman/hello/ExceptionUncaught",
            {"dev/roman/hello/ExceptionUncaught"},
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            false,
            {},
            {"<main>:java/lang/RuntimeException"},
            {},
        },
        TestCase{
            "uncaught thread exception",
            "dev/roman/hello/ExceptionThreadMidlet",
            {
                "dev/roman/hello/ExceptionThreadMidlet",
                "dev/roman/hello/ExceptionThreadTask",
            },
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            true,
            {},  // no expected pixels — no displayable means no present, framebuffer is unobservable
            {"dev/roman/hello/ExceptionThreadTask.run()V:java/lang/RuntimeException"},
            {"dev/roman/hello/ExceptionThreadTask.run()V:java/lang/RuntimeException"},
            false,  // expectsPresent: no displayable set → no host.present call
        },
        TestCase{
            "long arithmetic",
            "dev/roman/hello/LongArithmetic",
            {"dev/roman/hello/LongArithmetic"},
            {"24", "17", "42", "6", "2", "-5"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "int divide by two",
            "dev/roman/hello/DivByTwo",
            {"dev/roman/hello/DivByTwo"},
            {"3", "4", "-1", "-2", "0", "0"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "canvas key events",
            "dev/roman/hello/InputMidlet",
            {"dev/roman/hello/InputMidlet", "dev/roman/hello/InputCanvas"},
            {"pressed:-3", "left-code:-3", "released:-3"},
            {},
            {},
            {},
            {},
            {"display#1.setCurrent(obj#2)"},
            {
                "setColor(255,255,255)",
                "fillRect(0,0,240,320)",
                "setColor(0,0,0)",
                "fillRect(1,1,1,1)",
            },
            true,
        },
        TestCase{
            "graphics setColor int",
            "dev/roman/hello/GraphicsColorMidlet",
            {"dev/roman/hello/GraphicsColorMidlet", "dev/roman/hello/GraphicsColorCanvas"},
            {},
            {},
            {},
            {},
            {},
            {"display#1.setCurrent(obj#2)"},
            {
                "setColor(255)",
                "fillRect(0,0,240,320)",
                "setColor(16711680)",
                "fillRect(0,0,10,10)",
            },
            true,
            {{0, 0xf800}, {4820, 0x001f}},
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
        TestCase{
            "font dispatch",
            "dev/roman/hello/VirtualDispatchTest",
            {"dev/roman/hello/VirtualDispatchTest"},
            {"8", "29"},
            {}, {}, {}, {}, {}, {},
        },
        TestCase{
            "string null indexOf",
            "dev/roman/hello/StringNullIndexOf",
            {"dev/roman/hello/StringNullIndexOf"},
            {"-1"},
            {},
            {},
            {},
            {},
            {},
            {},
        },
        TestCase{
            "ByteArrayInputStream read",
            "dev/roman/hello/ByteArrayInputStreamTest",
            {"dev/roman/hello/ByteArrayInputStreamTest"},
            {"10", "20", "200", "255", "-1", "-1"},
            {}, {}, {}, {}, {}, {},
        },
        TestCase{
            "display notify lifecycle",
            "dev/roman/hello/DisplayNotifyMidlet",
            {"dev/roman/hello/DisplayNotifyMidlet", "dev/roman/hello/DisplayNotifyCanvas"},
            {"first:show", "first:hide", "second:show"},
            {},
            {},
            {},
            {},
            {"display#1.setCurrent(obj#2)", "display#1.setCurrent(obj#4)"},
            {
                "setColor(16777215)",
                "fillRect(0,0,240,320)",
                "setColor(0)",
                "fillRect(0,0,1,1)",
            },
            true,
            {{0, 0x0000}, {1, 0xffff}},
        },
    };

    try {
        size_t failed = 0;
        for (const TestCase& test : tests) {
            if (!runCase(root, test)) {
                ++failed;
            }
        }
        if (!runStepLimitPreemptionCase(root)) {
            ++failed;
        }
        if (!runNestedTaskSleepCase(root)) {
            ++failed;
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
