#pragma once

#include "class_file.hpp"
#include "interpreter.hpp"
#include "jvm_host.hpp"

#include <string>
#include <vector>

namespace jvmpoc {

class JvmMidletApp {
public:
    explicit JvmMidletApp(JvmHost& host);

    void loadClasses(const std::vector<std::string>& classFiles);
    void setClasses(std::vector<ClassFile> classes);
    const std::vector<ClassFile>& classes() const { return classes_; }

    const ExecutionTrace& start(const std::string& className);
    const ExecutionTrace& render();
    const ExecutionTrace& lastTrace() const { return lastTrace_; }
    const std::string& midletClassName() const { return midletClassName_; }

private:
    JvmHost& host_;
    std::vector<ClassFile> classes_;
    std::string midletClassName_;
    ExecutionTrace lastTrace_;
};

} // namespace jvmpoc
