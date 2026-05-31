#include "handlers.hpp"

#include "helpers.hpp"
#include "../j2me_port/J2MECompat.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#if !defined(ESP32_BUILD)
#include <filesystem>
#endif

namespace jvmpoc::native_methods {
namespace {

struct RmsStore {
    std::vector<std::vector<uint8_t>> records;
};

std::string withTrailingSlash(std::string path) {
    if (path.empty()) {
        return "./";
    }
    if (path.back() != '/' && path.back() != '\\') {
        path.push_back('/');
    }
    return path;
}

std::string sanitizeStoreName(std::string name) {
    if (name.empty()) {
        name = "default";
    }
    for (char& c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '\0') {
            c = '_';
        }
    }
    return name;
}

std::string rmsBaseDir() {
    return withTrailingSlash(port::resourceRoot()) + "rms";
}

std::string rmsPath(const std::string& name) {
    return rmsBaseDir() + "/" + sanitizeStoreName(name) + ".rms";
}

bool ensureDirTree(const std::string& dirPath) {
    esp_gallery::Fs* fs = port::resourceFs();
    if (fs == nullptr) {
#if !defined(ESP32_BUILD)
        std::error_code ec;
        std::filesystem::create_directories(dirPath, ec);
        return !ec && std::filesystem::is_directory(dirPath, ec);
#else
        return false;
#endif
    }

    std::string acc;
    acc.reserve(dirPath.size());
    for (char c : dirPath) {
        acc.push_back(c);
        if (c == '/') {
            if (acc == "/") {
                continue;
            }
            if (!fs->exists(acc.c_str())) {
                fs->mkdir(acc.c_str());
            }
        }
    }
    if (!fs->exists(acc.c_str())) {
        fs->mkdir(acc.c_str());
    }
    return fs->exists(acc.c_str());
}

bool readFileAll(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();
    esp_gallery::Fs* fs = port::resourceFs();
    if (fs == nullptr) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }

    auto file = fs->open(path.c_str(), FILE_READ, false);
    if (!file.isOpen()) {
        return false;
    }
    uint8_t buf[256];
    while (true) {
        const size_t count = file.read(buf, sizeof(buf));
        if (count == 0) {
            break;
        }
        out.insert(out.end(), buf, buf + count);
    }
    file.close();
    return true;
}

bool writeFileAll(const std::string& path, const std::vector<uint8_t>& data) {
    if (!ensureDirTree(rmsBaseDir())) {
        return false;
    }

    esp_gallery::Fs* fs = port::resourceFs();
    if (fs == nullptr) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        return static_cast<bool>(out);
    }

    auto file = fs->open(path.c_str(), FILE_WRITE, true);
    if (!file.isOpen()) {
        return false;
    }
    const size_t written = data.empty() ? 0 : file.write(data.data(), data.size());
    file.close();
    return written == data.size();
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t pos) {
    return (static_cast<uint32_t>(data[pos]) << 24) |
           (static_cast<uint32_t>(data[pos + 1]) << 16) |
           (static_cast<uint32_t>(data[pos + 2]) << 8) |
           static_cast<uint32_t>(data[pos + 3]);
}

void writeU32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    data.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    data.push_back(static_cast<uint8_t>(value & 0xff));
}

bool decodeStore(const std::vector<uint8_t>& data, RmsStore& store) {
    store.records.clear();
    if (data.empty()) {
        return true;
    }
    if (data.size() < 8 || data[0] != 'R' || data[1] != 'M' || data[2] != 'S' || data[3] != '1') {
        store.records.push_back(data);
        return true;
    }

    const uint32_t count = readU32(data, 4);
    size_t pos = 8;
    store.records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 4 > data.size()) {
            return false;
        }
        const uint32_t len = readU32(data, pos);
        pos += 4;
        if (pos + len > data.size()) {
            return false;
        }
        store.records.push_back(std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                                     data.begin() + static_cast<std::ptrdiff_t>(pos + len)));
        pos += len;
    }
    return true;
}

std::vector<uint8_t> encodeStore(const RmsStore& store) {
    std::vector<uint8_t> data;
    data.reserve(8 + store.records.size() * 8);
    data.push_back('R');
    data.push_back('M');
    data.push_back('S');
    data.push_back('1');
    writeU32(data, static_cast<uint32_t>(store.records.size()));
    for (const std::vector<uint8_t>& record : store.records) {
        writeU32(data, static_cast<uint32_t>(record.size()));
        data.insert(data.end(), record.begin(), record.end());
    }
    return data;
}

bool loadStore(const std::string& name, RmsStore& store, bool* existsOut = nullptr) {
    std::vector<uint8_t> data;
    const bool exists = readFileAll(rmsPath(name), data);
    if (existsOut != nullptr) {
        *existsOut = exists;
    }
    if (!exists) {
        store.records.clear();
        return false;
    }
    return decodeStore(data, store);
}

bool saveStore(const std::string& name, const RmsStore& store) {
    return writeFileAll(rmsPath(name), encodeStore(store));
}

bool deleteStoreFile(const std::string& name) {
    const std::string path = rmsPath(name);
    esp_gallery::Fs* fs = port::resourceFs();
    if (fs == nullptr) {
#if !defined(ESP32_BUILD)
        std::error_code ec;
        const bool removed = std::filesystem::remove(path, ec);
        return removed || (!ec && !std::filesystem::exists(path, ec));
#else
        return false;
#endif
    }

    if (!fs->exists(path.c_str())) {
        return true;
    }
    return fs->remove(path.c_str());
}

std::string runtimeString(const NativeCallContext& ctx, const Value& value) {
    std::optional<uint32_t> id = objectId(value);
    auto it = id.has_value() ? ctx.strings.find(*id) : ctx.strings.end();
    return it != ctx.strings.end() ? it->second : value.asText();
}

std::string storeNameFromReceiver(NativeCallContext& ctx, const std::vector<Value>& args) {
    if (args.empty()) {
        return "";
    }
    return runtimeString(ctx, ctx.readField(args[0], "name"));
}

bool bytesFromJavaArray(
    NativeCallContext& ctx,
    const Value& array,
    int offset,
    int length,
    std::vector<uint8_t>& out) {
    std::optional<uint32_t> id = arrayId(array);
    if (!id.has_value() || offset < 0 || length < 0) {
        return false;
    }
    auto primIt = ctx.primitiveArrays.find(*id);
    if (primIt != ctx.primitiveArrays.end()) {
        if (length > static_cast<int>(primIt->second.size()) - offset) return false;
        out.resize(static_cast<size_t>(length));
        for (int i = 0; i < length; ++i)
            out[static_cast<size_t>(i)] = static_cast<uint8_t>(primIt->second[static_cast<size_t>(offset + i)] & 0xff);
        return true;
    }
    auto it = ctx.arrays.find(*id);
    if (it == ctx.arrays.end() || length > static_cast<int>(it->second.size()) - offset) {
        return false;
    }
    out.resize(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        std::optional<int> value = parseIntValue(it->second[static_cast<size_t>(offset + i)]);
        out[static_cast<size_t>(i)] = static_cast<uint8_t>((value.value_or(0)) & 0xff);
    }
    return true;
}

bool copyRecordToJavaArray(
    NativeCallContext& ctx,
    const std::vector<uint8_t>& record,
    const Value& array,
    int offset,
    int length) {
    std::optional<uint32_t> id = arrayId(array);
    if (!id.has_value() || offset < 0 || length < 0 ||
        length > static_cast<int>(record.size())) {
        return false;
    }
    auto primIt = ctx.primitiveArrays.find(*id);
    if (primIt != ctx.primitiveArrays.end()) {
        if (length > static_cast<int>(primIt->second.size()) - offset) return false;
        for (int i = 0; i < length; ++i)
            primIt->second[static_cast<size_t>(offset + i)] =
                static_cast<int32_t>(static_cast<int8_t>(record[static_cast<size_t>(i)]));
        return true;
    }
    auto it = ctx.arrays.find(*id);
    if (it == ctx.arrays.end() || length > static_cast<int>(it->second.size()) - offset) {
        return false;
    }
    for (int i = 0; i < length; ++i) {
        it->second[static_cast<size_t>(offset + i)] =
            Value::ofInt(static_cast<int>(static_cast<int8_t>(record[static_cast<size_t>(i)])));
    }
    return true;
}

// --- Static leaves ---

NativeCallResult nm_rms_open0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = args.empty() ? "" : runtimeString(ctx, args[0]);
    const bool create = args.size() > 1 && parseIntValue(args[1]).value_or(0) != 0;
    RmsStore store;
    bool exists = false;
    if (loadStore(name, store, &exists)) {
        return handledValue(Value::ofInt(1));
    }
    if (!create) {
        return handledValue(Value::ofInt(0));
    }
    store.records.clear();
    return handledValue(Value::ofInt(saveStore(name, store) ? 1 : 0));
}

NativeCallResult nm_rms_delete0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = args.empty() ? "" : runtimeString(ctx, args[0]);
    return handledValue(Value::ofInt(deleteStoreFile(name) ? 1 : 0));
}

// --- Instance leaves --- (args[0] = receiver RecordStore)

NativeCallResult nm_rms_addRecord0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = storeNameFromReceiver(ctx, args);
    if (name.empty() || args.size() < 4) {
        return handledValue(Value::ofInt(0));
    }
    RmsStore store;
    (void)loadStore(name, store);
    std::vector<uint8_t> record;
    if (!bytesFromJavaArray(ctx, args[1], intArg(args, 2), intArg(args, 3), record)) {
        return handledValue(Value::ofInt(0));
    }
    store.records.push_back(std::move(record));
    if (!saveStore(name, store)) {
        return handledValue(Value::ofInt(0));
    }
    return handledValue(Value::ofInt(static_cast<int32_t>(store.records.size())));
}

NativeCallResult nm_rms_getNumRecords0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = storeNameFromReceiver(ctx, args);
    if (name.empty()) {
        return handledValue(Value::ofInt(0));
    }
    RmsStore store;
    if (!loadStore(name, store)) {
        return handledValue(Value::ofInt(0));
    }
    return handledValue(Value::ofInt(static_cast<int32_t>(store.records.size())));
}

NativeCallResult nm_rms_setRecord0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = storeNameFromReceiver(ctx, args);
    if (name.empty() || args.size() < 5) {
        return handledValue(Value::ofInt(0));
    }
    RmsStore store;
    if (!loadStore(name, store)) {
        return handledValue(Value::ofInt(0));
    }
    const int recordId = intArg(args, 1);
    if (recordId <= 0 || recordId > static_cast<int>(store.records.size())) {
        return handledValue(Value::ofInt(0));
    }
    std::vector<uint8_t> record;
    if (!bytesFromJavaArray(ctx, args[2], intArg(args, 3), intArg(args, 4), record)) {
        return handledValue(Value::ofInt(0));
    }
    store.records[static_cast<size_t>(recordId - 1)] = std::move(record);
    return handledValue(Value::ofInt(saveStore(name, store) ? 1 : 0));
}

NativeCallResult nm_rms_getRecordSize0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = storeNameFromReceiver(ctx, args);
    if (name.empty()) {
        return handledValue(Value::ofInt(-1));
    }
    RmsStore store;
    if (!loadStore(name, store)) {
        return handledValue(Value::ofInt(-1));
    }
    const int recordId = intArg(args, 1);
    if (recordId <= 0 || recordId > static_cast<int>(store.records.size())) {
        return handledValue(Value::ofInt(-1));
    }
    return handledValue(Value::ofInt(static_cast<int32_t>(store.records[static_cast<size_t>(recordId - 1)].size())));
}

NativeCallResult nm_rms_getRecord0(
    NativeCallContext& ctx, uint32_t /*pc*/, const std::vector<Value>& args) {
    const std::string name = storeNameFromReceiver(ctx, args);
    if (name.empty() || args.size() < 5) {
        return handledValue(Value::ofInt(0));
    }
    RmsStore store;
    if (!loadStore(name, store)) {
        return handledValue(Value::ofInt(0));
    }
    const int recordId = intArg(args, 1);
    if (recordId <= 0 || recordId > static_cast<int>(store.records.size())) {
        return handledValue(Value::ofInt(0));
    }
    const std::vector<uint8_t>& record = store.records[static_cast<size_t>(recordId - 1)];
    return handledValue(Value::ofInt(copyRecordToJavaArray(ctx, record, args[2], intArg(args, 3), intArg(args, 4)) ? 1 : 0));
}

struct NativeMethodEntry {
    const char* name;
    const char* descriptor;
    NativeLeafFn fn;
};

constexpr NativeMethodEntry kRecordStoreStaticMethods[] = {
    {"open0",   "(Ljava/lang/String;Z)Z", &nm_rms_open0},
    {"delete0", "(Ljava/lang/String;)Z",  &nm_rms_delete0},
};

constexpr NativeMethodEntry kRecordStoreInstanceMethods[] = {
    {"addRecord0",     "([BII)I",  &nm_rms_addRecord0},
    {"getNumRecords0", "()I",      &nm_rms_getNumRecords0},
    {"setRecord0",     "(I[BII)Z", &nm_rms_setRecord0},
    {"getRecordSize0", "(I)I",     &nm_rms_getRecordSize0},
    {"getRecord0",     "(I[BII)Z", &nm_rms_getRecord0},
};

NativeLeafFn lookupLeaf(const NativeMethodEntry* table, size_t n,
                        const std::string& name, const std::string& descriptor) {
    for (size_t i = 0; i < n; ++i) {
        if (name == table[i].name && descriptor == table[i].descriptor) {
            return table[i].fn;
        }
    }
    return nullptr;
}

} // namespace

NativeLeafFn lookupRecordStoreStaticLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kRecordStoreStaticMethods,
                      sizeof(kRecordStoreStaticMethods) / sizeof(kRecordStoreStaticMethods[0]),
                      name, descriptor);
}

NativeLeafFn lookupRecordStoreInstanceLeaf(const std::string& name, const std::string& descriptor) {
    return lookupLeaf(kRecordStoreInstanceMethods,
                      sizeof(kRecordStoreInstanceMethods) / sizeof(kRecordStoreInstanceMethods[0]),
                      name, descriptor);
}

NativeCallResult handleRecordStore(
    NativeCallContext& ctx,
    const std::string& methodLabel,
    uint32_t pc,
    const MethodRef& ref,
    const std::vector<Value>& args) {
    if (NativeLeafFn leaf = lookupRecordStoreStaticLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    if (NativeLeafFn leaf = lookupRecordStoreInstanceLeaf(ref.name, ref.descriptor)) {
        ctx.callerLabel = methodLabel;
        return leaf(ctx, pc, args);
    }
    return NativeCallResult{};
}

} // namespace jvmpoc::native_methods
