#pragma once

#include "class_file.hpp"
#include "interpreter.hpp"
#include "jvm_host.hpp"

#include <cstdint>
#include <memory>
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
    // Provide an externally managed framebuffer (e.g. allocated in SRAM on ESP32).
    // Must be at least screenWidth * screenHeight elements. If not set, an
    // internal heap-allocated buffer is used.
    void setExternalFramebuffer(uint16_t* ptr, size_t size);
    const ExecutionTrace& lastTrace() const { return lastTrace_; }
    const std::string& midletClassName() const { return midletClassName_; }

private:
    JvmHost& host_;
    std::vector<ClassFile> classes_;
    std::string midletClassName_;
    std::shared_ptr<MidletSession> session_;
    uint16_t* externalFb_ = nullptr;
    size_t externalFbSize_ = 0;
    std::vector<uint16_t> framebuffer_;  // fallback if externalFb_ is not set
    ExecutionTrace lastTrace_;
};

} // namespace jvmpoc
