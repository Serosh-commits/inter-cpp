#include "bound_method.hpp"
#include "../vm.hpp"

void ObjBoundMethod::blacken(VM& vm) {
    vm.markValue(receiver);
    vm.markObject(reinterpret_cast<Obj*>(method));
}

std::string ObjBoundMethod::toString() const {
    return "<bound method>";
}
