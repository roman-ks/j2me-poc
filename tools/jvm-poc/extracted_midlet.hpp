#pragma once

#include "class_file.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace jvmpoc {

struct ExtractedMidlet {
    std::string root;
    std::string midletClass;
    std::vector<std::string> classFiles;
    std::vector<ClassFile> classes;
    std::unordered_map<std::string, std::string> appProperties;
};

bool isDirectory(const std::string& path);
std::string defaultBootClassRoot();
void appendClassesFromDirectory(std::vector<ClassFile>& classes, const std::string& root);
void appendDefaultBootClasses(std::vector<ClassFile>& classes);
std::string readMidletClassFromManifest(const std::string& root);
ExtractedMidlet loadExtractedMidlet(const std::string& root, const std::string& midletOverride = {});

} // namespace jvmpoc
