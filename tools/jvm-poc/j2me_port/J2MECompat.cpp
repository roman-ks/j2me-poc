#include "J2MECompat.hpp"

#include <algorithm>
#include <array>
#include <core/configs.h>
#include <core/log.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <string>
#if defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace port {

namespace {
esp_gallery::Fs* g_resourceFs = nullptr;
std::string g_gameName = "default_game";
std::string g_resourceRoot = FS_ROOT_PATH;
std::set<std::string> g_warnedMissingResources;

std::string withTrailingSlash(std::string path) {
    if (path.empty()) {
        return "./";
    }
    if (path.back() != '/' && path.back() != '\\') {
        path.push_back('/');
    }
    return path;
}

bool isReadableRegularFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

std::string resolveAssetPath(const std::string& rawPath) {
    if (rawPath.empty()) {
        return rawPath;
    }

    const std::string noSlash = (rawPath[0] == '/') ? rawPath.substr(1) : rawPath;
    const std::string root = withTrailingSlash(g_resourceRoot);
    const std::array<std::string, 6> candidates = {
        root + "esp_gallery_data/" + g_gameName + "/" + noSlash,
        root + "esp_gallery_data/" + noSlash,
        root + g_gameName + "/" + noSlash,
        root + noSlash,
        noSlash,
        rawPath
    };

    if (g_resourceFs == nullptr) {
        for (const auto& candidate : candidates) {
            if (isReadableRegularFile(candidate)) {
                return candidate;
            }
        }
        return noSlash;
    }

    for (const auto& candidate : candidates) {
        if (g_resourceFs->exists(candidate.c_str())) {
            return candidate;
        }
    }

    return candidates[0];
}

bool readFileAll(const std::string& path, std::vector<uint8_t>& out) {
    out.clear();

    if (g_resourceFs == nullptr) {
        if (!isReadableRegularFile(path)) {
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }

    auto file = g_resourceFs->open(path.c_str(), FILE_READ, false);
    if (file.isOpen() == false) {
        return false;
    }

    uint8_t buf[512];
    while (true) {
        const size_t n = file.read(buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        out.insert(out.end(), buf, buf + n);
    }
    file.close();
    return true;
}

bool ensureDirTree(const std::string& dirPath) {
    if (g_resourceFs == nullptr || dirPath.empty()) {
        return false;
    }

    std::string acc;
    acc.reserve(dirPath.size());
    for (char c : dirPath) {
        acc.push_back(c);
        if (c == '/') {
            if (acc == "/") {
                continue;
            }
            if (!acc.empty() && !g_resourceFs->exists(acc.c_str())) {
                g_resourceFs->mkdir(acc.c_str());
            }
        }
    }
    if (!acc.empty() && !g_resourceFs->exists(acc.c_str())) {
        g_resourceFs->mkdir(acc.c_str());
    }
    return g_resourceFs->exists(acc.c_str());
}

std::string recordStoreBaseDir() {
    return withTrailingSlash(g_resourceRoot) + "esp_gallery_data/" + g_gameName + "/save";
}

std::string sanitizeStoreName(const std::string& raw) {
    std::string out = raw;
    if (out.empty()) {
        out = "default";
    }
    for (char& c : out) {
        if (c == '/' || c == '\\' || c == ':' || c == '\0') {
            c = '_';
        }
    }
    return out;
}

std::string recordStorePathForName(const std::string& name) {
    return recordStoreBaseDir() + "/" + sanitizeStoreName(name) + ".rms";
}
} // namespace

void setResourceFs(esp_gallery::Fs* fs) {
    g_resourceFs = fs;
}

esp_gallery::Fs* resourceFs() {
    return g_resourceFs;
}

void setResourceRoot(const std::string& root) {
    g_resourceRoot = withTrailingSlash(root);
}

const std::string& resourceRoot() {
    return g_resourceRoot;
}

bool readResourceAll(const std::string& path, std::vector<uint8_t>& out) {
    const std::string resolved = resolveAssetPath(path);
    const bool ok = readFileAll(resolved, out);
    if (!ok) {
        const std::string key = path + " -> " + resolved;
        if (g_warnedMissingResources.insert(key).second) {
            LOGF_W("Failed to read resource: raw=%s resolved=%s", path.c_str(), resolved.c_str());
        }
    }
    return ok;
}

RecordStore RecordStore::openRecordStore(const std::string& name, bool createIfMissing) {
    RecordStore rs;
    rs.store_name_ = sanitizeStoreName(name);

    if (g_resourceFs == nullptr) {
        return rs;
    }

    const std::string baseDir = recordStoreBaseDir();
    if (createIfMissing) {
        ensureDirTree(baseDir);
    }

    auto file = g_resourceFs->open(rs.storagePath().c_str(), FILE_READ, false);
    if (file.isOpen()) {
        rs.record_len_ = static_cast<int>(file.read(rs.record_.data(), rs.record_.size()));
        rs.num_records_ = (rs.record_len_ > 0) ? 1 : 0;
        file.close();
    } else if (createIfMissing) {
        rs.num_records_ = 0;
    }

    return rs;
}

bool RecordStore::deleteRecordStore(const std::string& name) {
    if (g_resourceFs == nullptr) {
        return false;
    }

    const std::string path = recordStorePathForName(name);
    if (!g_resourceFs->exists(path.c_str())) {
        return true;
    }

    return g_resourceFs->remove(path.c_str());
}

void RecordStore::setGameName(const std::string& gameName) {
    g_gameName = sanitizeStoreName(gameName);
}

const std::string& RecordStore::gameName() {
    return g_gameName;
}

std::string RecordStore::storagePath() const {
    return recordStoreBaseDir() + "/" + store_name_ + ".rms";
}

int RecordStore::getRecordSize(int /*id*/) const {
    return record_len_;
}

void RecordStore::persist() const {
    if (store_name_.empty() || g_resourceFs == nullptr) {
        return;
    }

    if (!ensureDirTree(recordStoreBaseDir())) {
        return;
    }

    auto file = g_resourceFs->open(storagePath().c_str(), FILE_WRITE, true);
    if (file.isOpen() == false) {
        return;
    }

    if (num_records_ > 0 && record_len_ > 0) {
        file.write(record_.data(), static_cast<size_t>(record_len_));
    }
    file.close();
}

int RecordStore::addRecord(const uint8_t* data, int offset, int len) {
    if (!data || len <= 0 || offset < 0 || offset >= len) {
        return 0;
    }

    int copyLen = std::min(static_cast<int>(record_.size()), len - offset);
    std::copy_n(data + offset, copyLen, record_.begin());
    record_len_ = copyLen;
    num_records_ = std::max(1, num_records_);
    persist();
    return 1;
}

void RecordStore::setRecord(int /*id*/, const uint8_t* data, int offset, int len) {
    addRecord(data, offset, len);
}

int RecordStore::getRecord(int /*id*/, uint8_t* out, int maxLen) const {
    if (!out || maxLen <= 0) {
        return 0;
    }
    int copyLen = std::min(maxLen, record_len_);
    std::copy_n(record_.begin(), copyLen, out);
    return copyLen;
}

ByteSpan RecordStore::getRecord(int /*id*/) const {
    return ByteSpan{record_.data(), record_len_};
}

std::vector<uint8_t> RecordStore::getRecordCopy(int /*id*/) const {
    return std::vector<uint8_t>(record_.begin(), record_.begin() + record_len_);
}

void RecordStore::closeRecordStore() {
    persist();
}

Random::Random() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
}

int Random::nextInt() {
    return std::rand();
}

void ByteArrayOutputStream::writeByte(uint8_t b) {
    if (size_ >= static_cast<int>(kMax)) {
        return;
    }
    data_[static_cast<size_t>(size_++)] = b;
}

int ByteArrayOutputStream::toByteArray(uint8_t* out, int maxLen) const {
    if (!out || maxLen <= 0) {
        return 0;
    }
    int copyLen = std::min(maxLen, size_);
    std::copy_n(data_.begin(), copyLen, out);
    return copyLen;
}

ByteSpan ByteArrayOutputStream::toByteArray() const {
    return ByteSpan{data_.data(), size_};
}

void DataOutputStream::writeBoolean(bool value) {
    out_.writeByte(value ? 1u : 0u);
}

void DataOutputStream::writeByte(int value) {
    out_.writeByte(static_cast<uint8_t>(value & 0xFF));
}

void DataOutputStream::writeInt(int value) {
    // Java DataOutputStream uses big-endian order.
    out_.writeByte(static_cast<uint8_t>((value >> 24) & 0xFF));
    out_.writeByte(static_cast<uint8_t>((value >> 16) & 0xFF));
    out_.writeByte(static_cast<uint8_t>((value >> 8) & 0xFF));
    out_.writeByte(static_cast<uint8_t>(value & 0xFF));
}

void DataOutputStream::writeShort(int value) {
    out_.writeByte(static_cast<uint8_t>((value >> 8) & 0xFF));
    out_.writeByte(static_cast<uint8_t>(value & 0xFF));
}

void DataOutputStream::writeUTF(const std::string& value) {
    const size_t writeLen = std::min<size_t>(value.size(), 0xFFFFu);
    writeShort(static_cast<int>(writeLen));
    for (size_t i = 0; i < writeLen; ++i) {
        out_.writeByte(static_cast<uint8_t>(value[i] & 0x7F));
    }
}

bool DataInputStream::readBoolean() {
    return readByte() != 0;
}

int DataInputStream::readByte() {
    if (in_.pos_ >= in_.len_ || !in_.data_) {
        return 0;
    }

    return static_cast<int8_t>(in_.data_[in_.pos_++]);
}

void DataInputStream::readFully(uint8_t* out, int len) {
    if (out == nullptr || len <= 0) {
        return;
    }

    for (int i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(readByte() & 0xFF);
    }
}

int DataInputStream::readInt() {
    if (in_.pos_ + 4 > in_.len_ || !in_.data_) {
        return 0;
    }

    int b0 = in_.data_[in_.pos_++];
    int b1 = in_.data_[in_.pos_++];
    int b2 = in_.data_[in_.pos_++];
    int b3 = in_.data_[in_.pos_++];

    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

int DataInputStream::readShort() {
    if (in_.pos_ + 2 > in_.len_ || !in_.data_) {
        return 0;
    }

    const int b0 = in_.data_[in_.pos_++];
    const int b1 = in_.data_[in_.pos_++];
    return static_cast<int16_t>((b0 << 8) | b1);
}

int DataInputStream::skipBytes(int n) {
    if (n <= 0 || !in_.data_) {
        return 0;
    }

    const int available = std::max(0, in_.len_ - in_.pos_);
    const int skip = std::min(n, available);
    in_.pos_ += skip;
    return skip;
}

std::string DataInputStream::readUTF() {
    const int len = readShort();
    if (len <= 0 || in_.data_ == nullptr) {
        return std::string();
    }

    const int available = std::max(0, in_.len_ - in_.pos_);
    const int copyLen = std::min(len, available);
    std::string out;
    out.reserve(static_cast<size_t>(copyLen));
    for (int i = 0; i < copyLen; ++i) {
        out.push_back(static_cast<char>(in_.data_[in_.pos_++] & 0x7F));
    }
    return out;
}

JavaString::JavaString(ByteSpan bytes) {
    if (bytes.data && bytes.len > 0) {
        value_.assign(reinterpret_cast<const char*>(bytes.data), static_cast<size_t>(bytes.len));
    }
}

int JavaString::length() const {
    return static_cast<int>(value_.size());
}

int JavaString::toCharArray(char* out, int maxLen) const {
    if (!out || maxLen <= 0) {
        return 0;
    }
    int n = std::min(maxLen, static_cast<int>(value_.size()));
    std::copy_n(value_.data(), n, out);
    return n;
}

std::string JavaString::substring(int start, int endExclusive) const {
    int len = static_cast<int>(value_.size());
    if (start < 0) {
        start = 0;
    }
    if (start > len) {
        start = len;
    }

    if (endExclusive < 0 || endExclusive > len) {
        endExclusive = len;
    }
    if (endExclusive < start) {
        endExclusive = start;
    }

    return value_.substr(static_cast<size_t>(start), static_cast<size_t>(endExclusive - start));
}

int Integer::parseInt(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

std::string Integer::toString(int value) {
    return std::to_string(value);
}

std::string String::valueOf(int value) {
    return std::to_string(value);
}

std::string String::valueOf(char value) {
    return std::string(1, value);
}

int ResourceLoader::read(const std::string& path, uint8_t* out, int maxLen) {
    if (out == nullptr || maxLen <= 0) {
        return 0;
    }

    std::vector<uint8_t> data;
    if (readResourceAll(path, data) == false || data.empty()) {
        return 0;
    }

    const int copyLen = std::min(maxLen, static_cast<int>(data.size()));
    std::copy_n(data.data(), copyLen, out);
    return copyLen;
}

} // namespace port
