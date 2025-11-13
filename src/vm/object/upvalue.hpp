#pragma once
#include "object.hpp"
#include "../value.hpp"

class ObjUpvalue : public Obj {
public:
    Value* location;
    Value closed = nullptr;
    ObjUpvalue* next = nullptr;

    ObjUpvalue(Value* slot) : Obj(Type::UPVALUE), location(slot) {}
};
