#include "handlers.hpp"

#include "helpers.hpp"

namespace jvmpoc::native_methods {

NativeCallResult handleSystem(
	NativeCallContext& ctx,
	const std::string& methodLabel,
	uint32_t pc,
	const MethodRef& ref,
	const std::vector<Value>& args) {
	(void)args;

	if (ref.name == "gc" && ref.descriptor == "()V") {
		ctx.collectGarbage(methodLabel + " pc=" + std::to_string(pc));
		return handledVoid();
	}

	return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
