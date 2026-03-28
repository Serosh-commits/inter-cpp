#include "upvalue.hpp"
#include "../vm.hpp"

void ObjUpvalue::blacken(VM& vm) {
    vm.markValue(closed);
}

std::string ObjUpvalue::toString() const {
    return "upvalue";
}
