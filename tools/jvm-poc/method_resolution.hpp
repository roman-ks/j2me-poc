#pragma once

#include "class_file.hpp"

#include <string>
#include <vector>

namespace jvmpoc {

const ClassFile* findClass(const std::vector<ClassFile>& classes, const std::string& name);
const MethodInfo* findDeclaredMethod(const ClassFile& cls, const std::string& name, const std::string& descriptor);
const MethodInfo* findMethodInHierarchy(
    const std::vector<ClassFile>& classes,
    const ClassFile& startClass,
    const std::string& name,
    const std::string& descriptor,
    const ClassFile** ownerOut = nullptr);
const MethodInfo* findMethodInHierarchy(
    const std::vector<ClassFile>& classes,
    const std::string& startClassName,
    const std::string& name,
    const std::string& descriptor,
    const ClassFile** ownerOut = nullptr);

} // namespace jvmpoc
