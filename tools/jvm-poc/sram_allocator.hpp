#pragma once

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

// SramAllocator<T>
//
// On ESP32 builds: allocates through heap_caps_malloc with MALLOC_CAP_INTERNAL
// so that the buffer always lands in internal SRAM regardless of the default
// PSRAM-first allocator configured by CONFIG_SPIRAM_USE_MALLOC.
//
// On all other builds: identical to std::allocator — zero overhead.

#ifdef ESP32_BUILD
#include <esp_heap_caps.h>
#endif

namespace jvmpoc {

template <typename T>
struct SramAllocator {
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    SramAllocator() noexcept = default;
    template <typename U>
    SramAllocator(const SramAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
#ifdef ESP32_BUILD
        void* ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (ptr == nullptr) {
            // Fallback to default allocator if SRAM is full
            ptr = ::operator new(n * sizeof(T));
        }
        return static_cast<T*>(ptr);
#else
        return static_cast<T*>(::operator new(n * sizeof(T)));
#endif
    }

    void deallocate(T* p, std::size_t /*n*/) noexcept {
#ifdef ESP32_BUILD
        heap_caps_free(p);
#else
        ::operator delete(p);
#endif
    }

    template <typename U>
    bool operator==(const SramAllocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const SramAllocator<U>&) const noexcept { return false; }
};

} // namespace jvmpoc
