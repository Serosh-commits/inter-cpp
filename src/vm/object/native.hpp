#pragma once
#include "object.hpp"
#include <functional>
#include "../value.hpp"

using NativeFn = std::function<Value(VM&, const std::vector<Value>&)>;

class ObjNative : public Obj {
public:
    NativeFn function;
    int arity;

    ObjNative(NativeFn function, int arity)
        : Obj(Type::NATIVE), function(function), arity(arity) {}
};
