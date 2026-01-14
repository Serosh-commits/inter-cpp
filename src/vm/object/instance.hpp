#pragma once
#include "object.hpp"
#include "class.hpp"
#include "../value.hpp"

class ObjInstance : public Obj {
public:
    ObjClass* klass;
    std::unordered_map<std::string, Value> fields;

    ObjInstance(ObjClass* k) : Obj(Type::INSTANCE), klass(k) {}
};
