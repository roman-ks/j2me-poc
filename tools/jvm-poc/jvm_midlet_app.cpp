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
    appendDefaultBootClasses(classes_);
}

const ExecutionTrace& JvmMidletApp::start(const std::string& className) {
    midletClassName_ = className;
    session_ = createMidletSession(classes_, className, &host_);
    lastTrace_ = startMidletSession(*session_);
    return lastTrace_;
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

    framebuffer_.assign(static_cast<size_t>(width * height), 0x39e7);
    lastTrace_ = renderMidletSession(*session_, framebuffer_, width, height);
    host_.present(framebuffer_.data(), width, height);
    return lastTrace_;
}

} // namespace jvmpoc
