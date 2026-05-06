#include "method_execution_delegate.hpp"

#include <core/log.h>

#include <chrono>
#include <string>

namespace jvmpoc {
namespace {

bool shouldTimeMethod(const ClassFile& cls, const MethodInfo& method) {
    return cls.thisClass == "GameScreen" && method.name == "make_buf" && method.descriptor == "(II)V";
}

std::string methodLabel(const ClassFile& cls, const MethodInfo& method) {
    return cls.thisClass + "." + method.name + method.descriptor;
}

} // namespace

std::optional<Value> delegateMethodExecution(
    const ClassFile& cls,
    const MethodInfo& method,
    const std::vector<Value>& args,
    const std::function<std::optional<Value>()>& invoke) {
    if (!shouldTimeMethod(cls, method)) {
        return invoke();
    }

    const auto startedAt = std::chrono::steady_clock::now();
    std::optional<Value> result = invoke();
    const uint64_t durationMicros = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - startedAt).count());
    const std::string widthArg = args.size() > 1 ? args[1].text : "<missing>";
    const std::string heightArg = args.size() > 2 ? args[2].text : "<missing>";
    LOGF_I(
        "%s width=%s height=%s took=%lluus",
        methodLabel(cls, method).c_str(),
        widthArg.c_str(),
        heightArg.c_str(),
        static_cast<unsigned long long>(durationMicros));
    return result;
}

} // namespace jvmpoc