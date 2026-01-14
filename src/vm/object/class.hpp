#pragma once
#include "object.hpp"
#include "string.hpp"
#include "../value.hpp"

class ObjClass : public Obj {
public:
    ObjString* name;
    std::unordered_map<std::string, Value> methods;
    ObjClass* superclass = nullptr;

    ObjClass(ObjString* n) : Obj(Type::CLASS), name(n) {}
};
