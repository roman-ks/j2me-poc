#include "jvm_midlet_app.hpp"

#include "extracted_midlet.hpp"

namespace jvmpoc {

JvmMidletApp::JvmMidletApp(JvmHost& host) : host_(host) {}

void JvmMidletApp::loadClasses(const std::vector<std::string>& classFiles) {
    classes_.clear();
    classes_.reserve(classFiles.size());
    for (const std::string& path : classFiles) {
        classes_.push_back(parseClassFile(path));
    }
    appendDefaultBootClasses(classes_);
}

void JvmMidletApp::setClasses(std::vector<ClassFile> classes) {
    classes_ = std::move(classes);
}

const ExecutionTrace& JvmMidletApp::start(const std::string& className) {
    midletClassName_ = className;
    session_ = createMidletSession(classes_, className, &host_);
#ifdef ESP32_BUILD
    jvmpoc::setTraceRecording(*session_, false);
#endif
    lastTrace_ = startMidletSession(*session_);
    return lastTrace_;
}

void JvmMidletApp::setExternalFramebuffer(uint16_t* ptr, size_t size) {
    externalFb_ = ptr;
    externalFbSize_ = size;
}

void JvmMidletApp::setTraceRecording(bool enabled) {
    if (session_) jvmpoc::setTraceRecording(*session_, enabled);
}

const ExecutionTrace& JvmMidletApp::render() {
    lastTrace_ = ExecutionTrace{};
    const int width = host_.screenWidth();
    const int height = host_.screenHeight();
    if (width <= 0 || height <= 0) {
        return lastTrace_;
    }
    if (!session_) {
        return lastTrace_;
    }

    const size_t expectedSize = static_cast<size_t>(width * height);
    uint16_t* fb;
    if (externalFb_ != nullptr && externalFbSize_ >= expectedSize) {
        fb = externalFb_;
    } else {
        if (framebuffer_.size() != expectedSize) {
            framebuffer_.assign(expectedSize, 0x39e7);
        }
        fb = framebuffer_.data();
    }
    lastTrace_ = renderMidletSession(*session_, fb, width, height);
    if (lastTrace_.framePresented) {
        host_.present(fb, width, height);
    }
    return lastTrace_;
}

} // namespace jvmpoc
