#pragma once
#include "object.hpp"
#include "../chunk.hpp"

class ObjFunction : public Obj {
public:
    int arity = 0;
    int upvalueCount = 0;
    Chunk chunk;
    ObjString* name = nullptr;

    ObjFunction() : Obj(Type::FUNCTION) {}
};
