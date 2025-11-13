#include "string.hpp"
#include "../vm.hpp"

ObjString::ObjString(std::string s) : Obj(Type::STRING), str(std::move(s)) {
    hash = 2166136261u;
    for (char c : str) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619;
    }
}

ObjString* ObjString::copyString(VM& vm, const char* chars, int length) {
    std::string s(chars, length);
    return vm.allocateString(std::move(s));
}

ObjString* ObjString::takeString(VM& vm, char* chars, int length) {
    std::string s(chars, length);
    delete[] chars;
    return vm.allocateString(std::move(s));
}
