#pragma once
#include "object.hpp"
#include "../value.hpp"
#include <vector>

class ObjList : public Obj {
public:
    std::vector<Value> elements;

    ObjList() : Obj(Type::LIST) {}
    void blacken(VM& vm) override;
    std::string toString() const override;
};
