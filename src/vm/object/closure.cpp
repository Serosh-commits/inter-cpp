#include "closure.hpp"
#include "function.hpp"
#include "string.hpp"
#include "upvalue.hpp"
#include "../vm.hpp"

void ObjClosure::blacken(VM& vm) {
    vm.markObject(reinterpret_cast<Obj*>(function));
    for (ObjUpvalue* upvalue : upvalues) {
        vm.markObject(reinterpret_cast<Obj*>(upvalue));
    }
}

std::string ObjClosure::toString() const {
    if (function->name == nullptr) return "<script>";
    return "<fn " + function->name->str + ">";
}
