#pragma once
#include "object.hpp"
#include "../value.hpp"
#include <vector>

class ObjList : public Obj {
public:
    std::vector<Value> elements;

    ObjList() : Obj(Type::LIST) {}
};
