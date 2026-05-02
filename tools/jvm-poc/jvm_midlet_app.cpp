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
    lastTrace_ = executeMidlet(classes_, className);
    return lastTrace_;
}

const ExecutionTrace& JvmMidletApp::render() {
    (void)host_;
    lastTrace_ = ExecutionTrace{};
    return lastTrace_;
}

} // namespace jvmpoc
