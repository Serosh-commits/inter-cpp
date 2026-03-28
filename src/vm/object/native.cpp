#include "native.hpp"
#include "../vm.hpp"

void ObjNative::blacken(VM& vm) {
}

std::string ObjNative::toString() const {
    return "<native fn>";
}
