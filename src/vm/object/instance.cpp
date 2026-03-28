#include "instance.hpp"
#include "string.hpp"
#include "../vm.hpp"

void ObjInstance::blacken(VM& vm) {
    vm.markObject(reinterpret_cast<Obj*>(klass));
    for (auto& pair : fields) {
        vm.markValue(pair.second);
    }
}

std::string ObjInstance::toString() const {
    return klass->name->str + " instance";
}
