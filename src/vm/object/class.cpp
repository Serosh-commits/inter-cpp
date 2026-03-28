#include "class.hpp"
#include "string.hpp"
#include "../vm.hpp"

void ObjClass::blacken(VM& vm) {
    vm.markObject(reinterpret_cast<Obj*>(name));
    for (auto& pair : methods) {
        vm.markValue(pair.second);
    }
}

std::string ObjClass::toString() const {
    return name->str;
}
