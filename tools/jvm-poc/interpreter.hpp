#pragma once

#include "class_file.hpp"
#include "frame.hpp"

#include <vector>

namespace jvmpoc {

std::vector<LocalWrite> inferStraightLineLocalWrites(const MethodInfo& method);

} // namespace jvmpoc
