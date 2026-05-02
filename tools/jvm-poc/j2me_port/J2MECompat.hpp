#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <core/fs.h>

namespace port {

struct ByteSpan {
    const uint8_t* data = nullptr;
    int len = 0;
};

class Default {
public:
    bool start_app = false;
    bool quit_requested = false;
    void startApp() {}
    void destroyApp(bool /*unconditional*/) { quit_requested = true; }
    void notifyDestroyed() { quit_requested = true; }
};

class CanvasHost {
public:
    void setFullScreenMode(bool enabled) { fullscreen_ = enabled; }
    bool isFullScreenMode() const { return fullscreen_; }
    void repaint() {}
    void serviceRepaints() {}

private:
    bool fullscreen_ = false;
};

class Display {
public:
    static Display* getDisplay(Default* /*owner*/) {
        static Display instance;
        return &instance;
    }

    void setCurrent(void* screen) { current_ = screen; }
    void setCurrent(void* screen, void* nextScreen) {
        current_ = screen;
        next_ = nextScreen;
    }
    void* current() const { return current_; }
    void* next() const { return next_; }

private:
    void* current_ = nullptr;
    void* next_ = nullptr;
};

class RecordStore {
public:
    static RecordStore openRecordStore(const std::string& name, bool createIfMissing);
    static bool deleteRecordStore(const std::string& name);
    static void setGameName(const std::string& gameName);
    static const std::string& gameName();

    int getNumRecords() const { return num_records_; }
    int getRecordSize(int id) const;

    int addRecord(const uint8_t* data, int offset, int len);
    void setRecord(int id, const uint8_t* data, int offset, int len);
    int getRecord(int id, uint8_t* out, int maxLen) const;
    ByteSpan getRecord(int id) const;
    std::vector<uint8_t> getRecordCopy(int id) const;
    void closeRecordStore();

private:
    std::string storagePath() const;
    void persist() const;

    std::string store_name_;
    int num_records_ = 0;
    std::array<uint8_t, 1024> record_{};
    int record_len_ = 0;
};

class AudioClip {
public:
    AudioClip() = default;
    AudioClip(int /*type*/, const std::string& resource) : resource_(resource) {}

    void play(int /*loops*/, int /*volume*/) {}
    void stop() {}

private:
    std::string resource_;
};

class Vibration {
public:
    static void start(int /*durationMs*/, int /*strength*/) {}
};

class Runtime {
public:
    static Runtime getRuntime() { return Runtime{}; }
};

class Thread {
public:
    template <typename Fn>
    explicit Thread(Fn&& /*fn*/) {}

    void start() {}
    static void sleep(long ms) {
        if (ms <= 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
};

class System {
public:
    static void gc() {}
};

class Random {
public:
    Random();
    int nextInt();
};

class ByteArrayOutputStream {
public:
    static constexpr size_t kMax = 1024;

    void writeByte(uint8_t b);
    int size() const { return size_; }
    int toByteArray(uint8_t* out, int maxLen) const;
    ByteSpan toByteArray() const;

private:
    std::array<uint8_t, kMax> data_{};
    int size_ = 0;

    friend class DataOutputStream;
};

class DataOutputStream {
public:
    explicit DataOutputStream(ByteArrayOutputStream& out) : out_(out) {}
    void writeBoolean(bool value);
    void writeByte(int value);
    void writeInt(int value);
    void writeShort(int value);
    void writeUTF(const std::string& value);

private:
    ByteArrayOutputStream& out_;
};

class ByteArrayInputStream {
public:
    ByteArrayInputStream(const uint8_t* data, int len) : data_(data), len_(len) {}
    explicit ByteArrayInputStream(ByteSpan bytes) : data_(bytes.data), len_(bytes.len) {}

private:
    const uint8_t* data_ = nullptr;
    int len_ = 0;
    int pos_ = 0;

    friend class DataInputStream;
};

class DataInputStream {
public:
    explicit DataInputStream(ByteArrayInputStream& in) : in_(in) {}
    bool readBoolean();
    int readByte();
    void readFully(uint8_t* out, int len);
    int readInt();
    int readShort();
    int skipBytes(int n);
    std::string readUTF();

private:
    ByteArrayInputStream& in_;
};

class JavaString {
public:
    JavaString() = default;
    explicit JavaString(const std::string& s) : value_(s) {}
    explicit JavaString(ByteSpan bytes);

    int length() const;
    int toCharArray(char* out, int maxLen) const;
    std::string substring(int start, int endExclusive = -1) const;

    const std::string& str() const { return value_; }

private:
    std::string value_;
};

class Integer {
public:
    static int parseInt(const std::string& s);
    static std::string toString(int value);
};

class String {
public:
    static std::string valueOf(int value);
    static std::string valueOf(char value);
};

class ResourceLoader {
public:
    // Reads up to maxLen bytes from a resource path into out. Returns bytes read.
    static int read(const std::string& path, uint8_t* out, int maxLen);
};

void setResourceFs(esp_gallery::Fs* fs);
esp_gallery::Fs* resourceFs();
bool readResourceAll(const std::string& path, std::vector<uint8_t>& out);

} // namespace port
