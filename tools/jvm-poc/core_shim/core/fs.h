#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace esp_gallery
{
    


#define FILE_READ       "r"
#define FILE_WRITE      "w"
#define FILE_APPEND     "a"

struct FileImpl {
    virtual ~FileImpl() = default;

    virtual size_t read(void* buf, size_t size) = 0;
    virtual size_t write(const void* buf, size_t size) = 0;
    virtual bool seek(uint32_t pos) = 0;
    virtual bool isOpen() const = 0;
    virtual void close() = 0;

    // clone is needed for value semantics without heap
    virtual void cloneTo(void* storage) const = 0;
};

class File {
private:
    static constexpr size_t StorageSize = 64;
    static constexpr size_t StorageAlign = alignof(std::max_align_t);

    using Storage = std::aligned_storage_t<StorageSize, StorageAlign>;
    Storage storage;
    FileImpl* impl = nullptr;
    void (*destroyFn)(void*) = nullptr;
    void (*moveFn)(void*, void*) = nullptr;

    template <typename Impl>
    static void destroyStorage(void* storagePtr) {
        static_cast<Impl*>(storagePtr)->~Impl();
    }

    template <typename Impl>
    static void moveStorage(void* dstStorage, void* srcStorage) {
        auto* srcImpl = static_cast<Impl*>(srcStorage);
        new (dstStorage) Impl(std::move(*srcImpl));
        srcImpl->~Impl();
    }

public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    template <typename Impl, typename... Args>
    void emplace(Args&&... args) {
        static_assert(sizeof(Impl) <= StorageSize, "Impl too large");
        static_assert(alignof(Impl) <= StorageAlign, "Impl alignment too strict");
        static_assert(std::is_base_of_v<FileImpl, Impl>, "Impl must derive from FileImpl");
        reset();
        impl = new (&storage) Impl(std::forward<Args>(args)...);
        destroyFn = &destroyStorage<Impl>;
        moveFn = &moveStorage<Impl>;
    }

    // move-only
    File(File&& other) noexcept {
        moveFrom(other);
    }

    File& operator=(File&& other) noexcept {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    ~File() { reset(); }

    void reset() {
        if (impl) {
            destroyFn(&storage);
            impl = nullptr;
            destroyFn = nullptr;
            moveFn = nullptr;
        }
    }

    void moveFrom(File& other) {
        if (!other.impl) {
            return;
        }

        other.moveFn(&storage, &other.storage);
        impl = reinterpret_cast<FileImpl*>(&storage);
        destroyFn = other.destroyFn;
        moveFn = other.moveFn;
        other.impl = nullptr;
        other.destroyFn = nullptr;
        other.moveFn = nullptr;
    }

    // methods
    size_t read(void* buf, size_t size) {
        return impl ? impl->read(buf, size) : 0;
    }

    size_t write(const void* buf, size_t size) {
        return impl ? impl->write(buf, size) : 0;
    }

    bool seek(uint32_t pos){
        return impl ? impl->seek(pos) : false;
    }

    bool isOpen() const { return impl && impl->isOpen(); }

    void close() {
        if (impl) {
            impl->close();
            reset();
        }
    }

};

// Abstract directory interface
class DirImpl {
public:
    virtual ~DirImpl() = default;
    virtual bool isOpen() const = 0;
    virtual void nextFilename(char * outPath, size_t outSize, bool *isDir) = 0;
    virtual void close() = 0;
};

// ESP wrapper holding DirImpl
class Dir {
private:
    static constexpr size_t StorageSize = 64;
    static constexpr size_t StorageAlign = alignof(std::max_align_t);

    using Storage = std::aligned_storage_t<StorageSize, StorageAlign>;
    Storage storage;
    DirImpl* impl = nullptr;
    void (*destroyFn)(void*) = nullptr;
    void (*moveFn)(void*, void*) = nullptr;

    template <typename Impl>
    static void destroyStorage(void* storagePtr) {
        static_cast<Impl*>(storagePtr)->~Impl();
    }

    template <typename Impl>
    static void moveStorage(void* dstStorage, void* srcStorage) {
        auto* srcImpl = static_cast<Impl*>(srcStorage);
        new (dstStorage) Impl(std::move(*srcImpl));
        srcImpl->~Impl();
    }
    
public:
    Dir() = default;
    Dir(const Dir&) = delete;
    Dir& operator=(const Dir&) = delete;

    template <typename Impl, typename... Args>
    void emplace(Args&&... args) {
        static_assert(sizeof(Impl) <= StorageSize, "Impl too large");
        static_assert(alignof(Impl) <= StorageAlign, "Impl alignment too strict");
        static_assert(std::is_base_of_v<DirImpl, Impl>, "Impl must derive from DirImpl");
        reset();
        impl = new (&storage) Impl(std::forward<Args>(args)...);
        destroyFn = &destroyStorage<Impl>;
        moveFn = &moveStorage<Impl>;
    }
    
    // move-only
    Dir(Dir&& other) noexcept {
        moveFrom(other);
    }

    // move assignment
    Dir& operator=(Dir&& other) noexcept {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    ~Dir() {
        reset(); 
    }

    void reset() {
        if (impl) {
            destroyFn(&storage);
            impl = nullptr;
            destroyFn = nullptr;
            moveFn = nullptr;
        }
    }

    void moveFrom(Dir& other) {
        if (!other.impl) {
            return;
        }

        other.moveFn(&storage, &other.storage);
        impl = reinterpret_cast<DirImpl*>(&storage);
        destroyFn = other.destroyFn;
        moveFn = other.moveFn;
        other.impl = nullptr;
        other.destroyFn = nullptr;
        other.moveFn = nullptr;
    }

    bool isOpen() const { return impl && impl->isOpen(); }

    void nextFilename(char * outPath, size_t outSize, bool *isDir) { 
        if (impl) 
            impl->nextFilename(outPath, outSize, isDir); 
    }

    void close() {
        if (impl) {
            impl->close();
            reset();
        }
    }
};


class Fs {
public:
    virtual ~Fs() = default;

    virtual bool mkdir(const char *) = 0;
    virtual bool exists(const char *) = 0;
    virtual bool rename(const char* pathFrom, const char* pathTo) = 0;
    virtual bool remove(const char *) = 0;

    virtual File open(const char* path, const char* mode = FILE_READ, const bool create = false) = 0;
    virtual Dir openDir(const char* path) = 0;

};

} // namespace esp_gallery
