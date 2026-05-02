#include "jvm_midlet_app.hpp"

namespace jvmpoc {

JvmMidletApp::JvmMidletApp(JvmHost& host) : host_(host) {}

void JvmMidletApp::loadClasses(const std::vector<std::string>& classFiles) {
    classes_.clear();
    classes_.reserve(classFiles.size());
    for (const std::string& path : classFiles) {
        classes_.push_back(parseClassFile(path));
    }
}

void JvmMidletApp::setClasses(std::vector<ClassFile> classes) {
    classes_ = std::move(classes);
}

const ExecutionTrace& JvmMidletApp::start(const std::string& className) {
    midletClassName_ = className;
    lastTrace_ = executeMidlet(classes_, className, &host_);
    if (!lastTrace_.displaySetCurrents.empty()) {
        currentDisplayable_ = lastTrace_.displaySetCurrents.back().displayable;
    }
    return lastTrace_;
}

const ExecutionTrace& JvmMidletApp::render() {
    lastTrace_ = ExecutionTrace{};
    const int width = host_.screenWidth();
    const int height = host_.screenHeight();
    if (width <= 0 || height <= 0) {
        return lastTrace_;
    }

    framebuffer_.assign(static_cast<size_t>(width * height), 0x39e7);
    lastTrace_ = renderMidletFrame(classes_, midletClassName_, &host_, framebuffer_.data(), width, height);
    if (!lastTrace_.displaySetCurrents.empty()) {
        currentDisplayable_ = lastTrace_.displaySetCurrents.back().displayable;
    }
    host_.present(framebuffer_.data(), width, height);
    return lastTrace_;
}

} // namespace jvmpoc
