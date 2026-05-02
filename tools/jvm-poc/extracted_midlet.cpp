#include "extracted_midlet.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace jvmpoc {
namespace {

std::string trim(std::string value) {
    auto begin = std::find_if(value.begin(), value.end(), [](unsigned char c) {
        return !std::isspace(c);
    });
    auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string classNameToInternal(std::string value) {
    std::replace(value.begin(), value.end(), '.', '/');
    return value;
}

std::vector<std::string> manifestLines(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    std::vector<std::string> lines;
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        if (!raw.empty() && raw.front() == ' ' && !lines.empty()) {
            lines.back() += raw.substr(1);
        } else {
            lines.push_back(raw);
        }
    }
    return lines;
}

std::string manifestValue(const std::vector<std::string>& lines, const std::string& key) {
    const std::string prefix = key + ":";
    for (const std::string& line : lines) {
        if (line.compare(0, prefix.size(), prefix) == 0) {
            return trim(line.substr(prefix.size()));
        }
    }
    return "";
}

std::string midletClassFromMidlet1(const std::string& value) {
    size_t firstComma = value.find(',');
    if (firstComma == std::string::npos) {
        return "";
    }
    size_t secondComma = value.find(',', firstComma + 1);
    if (secondComma == std::string::npos) {
        return "";
    }
    return classNameToInternal(trim(value.substr(secondComma + 1)));
}

bool isClassFile(const std::filesystem::path& path) {
    return path.has_extension() && path.extension() == ".class";
}

bool hasClass(const std::vector<ClassFile>& classes, const std::string& className) {
    for (const ClassFile& cls : classes) {
        if (cls.thisClass == className) {
            return true;
        }
    }
    return false;
}

} // namespace

bool isDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::string defaultBootClassRoot() {
    return "target/jvm-boot-classes";
}

void appendClassesFromDirectory(std::vector<ClassFile>& classes, const std::string& root) {
    if (!isDirectory(root)) {
        return;
    }

    std::vector<std::string> classFiles;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !isClassFile(entry.path())) {
            continue;
        }
        classFiles.push_back(entry.path().string());
    }
    std::sort(classFiles.begin(), classFiles.end());

    for (const std::string& path : classFiles) {
        ClassFile cls = parseClassFile(path);
        if (!hasClass(classes, cls.thisClass)) {
            classes.push_back(std::move(cls));
        }
    }
}

void appendDefaultBootClasses(std::vector<ClassFile>& classes) {
    appendClassesFromDirectory(classes, defaultBootClassRoot());
}

std::string readMidletClassFromManifest(const std::string& root) {
    std::filesystem::path manifestPath = std::filesystem::path(root) / "META-INF" / "MANIFEST.MF";
    std::vector<std::string> lines = manifestLines(manifestPath.string());
    std::string midlet1 = manifestValue(lines, "MIDlet-1");
    return midletClassFromMidlet1(midlet1);
}

ExtractedMidlet loadExtractedMidlet(const std::string& root, const std::string& midletOverride) {
    if (!isDirectory(root)) {
        throw std::runtime_error(root + " is not an extracted MIDlet directory");
    }

    ExtractedMidlet result;
    result.root = root;
    result.midletClass = midletOverride.empty() ? readMidletClassFromManifest(root) : classNameToInternal(midletOverride);
    if (result.midletClass.empty()) {
        throw std::runtime_error("cannot infer MIDlet class from " + root + "/META-INF/MANIFEST.MF");
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || !isClassFile(entry.path())) {
            continue;
        }
        result.classFiles.push_back(entry.path().string());
    }
    std::sort(result.classFiles.begin(), result.classFiles.end());

    result.classes.reserve(result.classFiles.size());
    for (const std::string& path : result.classFiles) {
        result.classes.push_back(parseClassFile(path));
    }
    appendDefaultBootClasses(result.classes);

    return result;
}

} // namespace jvmpoc
