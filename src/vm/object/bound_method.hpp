#pragma once
#include "object.hpp"
#include "closure.hpp"
#include "../value.hpp"

class ObjBoundMethod : public Obj {
public:
    Value receiver;
    ObjClosure* method;

    ObjBoundMethod(Value r, ObjClosure* m) : Obj(Type::BOUND_METHOD), receiver(r), method(m) {}
};
