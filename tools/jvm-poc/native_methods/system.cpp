#include "handlers.hpp"

#include "helpers.hpp"

#ifdef ESP32_BUILD
#include <esp_timer.h>
#else
#include <chrono>
#endif

namespace jvmpoc::native_methods {
namespace {

int64_t currentTimeMillis() {
#ifdef ESP32_BUILD
	return static_cast<int64_t>(esp_timer_get_time() / 1000);
#else
	return static_cast<int64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
#endif
}

} // namespace

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

	if (ref.name == "currentTimeMillis" && ref.descriptor == "()J") {
		return handledValue(Value::ofLong(currentTimeMillis()));
	}

	if (ref.name == "arraycopy" && ref.descriptor == "(Ljava/lang/Object;ILjava/lang/Object;II)V") {
		if (args.size() < 5) {
			return handledVoid();
		}

		std::optional<uint32_t> srcId = arrayId(args[0]);
		std::optional<int> srcPos = parseIntValue(args[1]);
		std::optional<uint32_t> destId = arrayId(args[2]);
		std::optional<int> destPos = parseIntValue(args[3]);
		std::optional<int> length = parseIntValue(args[4]);
		if (!srcId.has_value() || !destId.has_value() ||
			!srcPos.has_value() || !destPos.has_value() || !length.has_value()) {
			return handledVoid();
		}

		auto srcIt = ctx.arrays.find(*srcId);
		auto destIt = ctx.arrays.find(*destId);
		if (srcIt == ctx.arrays.end() || destIt == ctx.arrays.end() ||
			*srcPos < 0 || *destPos < 0 || *length < 0 ||
			*length > static_cast<int>(srcIt->second.size()) - *srcPos ||
			*length > static_cast<int>(destIt->second.size()) - *destPos) {
			return handledVoid();
		}

		if (*srcId == *destId && *destPos > *srcPos && *destPos < *srcPos + *length) {
			for (int i = *length - 1; i >= 0; --i) {
				destIt->second[static_cast<size_t>(*destPos + i)] =
					srcIt->second[static_cast<size_t>(*srcPos + i)];
			}
		} else {
			for (int i = 0; i < *length; ++i) {
				destIt->second[static_cast<size_t>(*destPos + i)] =
					srcIt->second[static_cast<size_t>(*srcPos + i)];
			}
		}
		return handledVoid();
	}

	return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
