#pragma once
#include "object.hpp"
#include "function.hpp"

class ObjUpvalue;

class ObjClosure : public Obj {
public:
    ObjFunction* function;
    std::vector<ObjUpvalue*> upvalues;

    ObjClosure(ObjFunction* f) : Obj(Type::CLOSURE), function(f) {
        upvalues.resize(f->upvalueCount);
    }
};
