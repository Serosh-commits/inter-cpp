#include "list.hpp"
#include "string.hpp"
#include "../vm.hpp"

void ObjList::blacken(VM& vm) {
    for (Value& val : elements) {
        vm.markValue(val);
    }
}

std::string ObjList::toString() const {
    std::string result = "[";
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) result += ", ";
        result += valueToString(elements[i]);
    }
    result += "]";
    return result;
}
