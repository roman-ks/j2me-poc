#pragma once

#include "class_file.hpp"
#include "value.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace jvmpoc {

std::optional<Value> delegateMethodExecution(
    const ClassFile& cls,
    const MethodInfo& method,
    std::vector<Value>& args,
    const std::function<std::optional<Value>()>& invoke);

} // namespace jvmpoc