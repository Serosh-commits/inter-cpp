#include "function.hpp"
#include "string.hpp"
#include "../vm.hpp"

void ObjFunction::blacken(VM& vm) {
    vm.markObject(reinterpret_cast<Obj*>(name));
    for (Value constant : chunk.constants) {
        vm.markValue(constant);
    }
}

std::string ObjFunction::toString() const {
    if (name == nullptr) return "<script>";
    return "<fn " + name->str + ">";
}
