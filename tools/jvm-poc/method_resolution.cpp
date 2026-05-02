#include "method_resolution.hpp"

namespace jvmpoc {

const ClassFile* findClass(const std::vector<ClassFile>& classes, const std::string& name) {
    for (const ClassFile& cls : classes) {
        if (cls.thisClass == name) {
            return &cls;
        }
    }
    return nullptr;
}

const MethodInfo* findDeclaredMethod(const ClassFile& cls, const std::string& name, const std::string& descriptor) {
    for (const MethodInfo& method : cls.methods) {
        if (method.name == name && method.descriptor == descriptor) {
            return &method;
        }
    }
    return nullptr;
}

const MethodInfo* findMethodInHierarchy(
    const std::vector<ClassFile>& classes,
    const ClassFile& startClass,
    const std::string& name,
    const std::string& descriptor,
    const ClassFile** ownerOut) {
    const ClassFile* current = &startClass;
    while (current != nullptr) {
        const MethodInfo* method = findDeclaredMethod(*current, name, descriptor);
        if (method != nullptr) {
            if (ownerOut != nullptr) {
                *ownerOut = current;
            }
            return method;
        }
        if (current->superClass.empty()) {
            break;
        }
        current = findClass(classes, current->superClass);
    }

    if (ownerOut != nullptr) {
        *ownerOut = nullptr;
    }
    return nullptr;
}

const MethodInfo* findMethodInHierarchy(
    const std::vector<ClassFile>& classes,
    const std::string& startClassName,
    const std::string& name,
    const std::string& descriptor,
    const ClassFile** ownerOut) {
    const ClassFile* startClass = findClass(classes, startClassName);
    if (startClass == nullptr) {
        if (ownerOut != nullptr) {
            *ownerOut = nullptr;
        }
        return nullptr;
    }
    return findMethodInHierarchy(classes, *startClass, name, descriptor, ownerOut);
}

bool isClassOrSubclassOf(
    const std::vector<ClassFile>& classes,
    const std::string& startClassName,
    const std::string& ancestorClassName) {
    std::string currentName = startClassName;
    while (!currentName.empty()) {
        if (currentName == ancestorClassName) {
            return true;
        }
        const ClassFile* current = findClass(classes, currentName);
        if (current == nullptr) {
            return false;
        }
        currentName = current->superClass;
    }
    return false;
}

} // namespace jvmpoc
