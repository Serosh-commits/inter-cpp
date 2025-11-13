#pragma once
#include "object.hpp"

class ObjString : public Obj {
public:
    std::string str;
    uint32_t hash;

    ObjString(std::string s);
    const char* c_str() const { return str.c_str(); }

    static ObjString* copyString(VM& vm, const char* chars, int length);
    static ObjString* takeString(VM& vm, char* chars, int length);
};
