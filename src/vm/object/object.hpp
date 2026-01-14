#pragma once
#include "common/common.hpp"

class VM;
class ObjString;
class ObjFunction;
class ObjClosure;
class ObjUpvalue;
class ObjClass;
class ObjInstance;
class ObjBoundMethod;

class Obj {
public:
    enum class Type { STRING, FUNCTION, CLOSURE, UPVALUE, CLASS, INSTANCE, BOUND_METHOD };
    Type type;
    bool marked = false;
    Obj* next = nullptr;

    explicit Obj(Type t) : type(t) {}
    virtual ~Obj() = default;
};

#define AS_OBJ(value)       (std::get<Obj*>(value))
#define AS_STRING(value)    (AS_OBJ(value)->as<ObjString>())
#define AS_CSTRING(value)   (AS_STRING(value)->c_str())
#define AS_FUNCTION(value)  (AS_OBJ(value)->as<ObjFunction>())
#define AS_CLOSURE(value)   (AS_OBJ(value)->as<ObjClosure>())
#define AS_CLASS(value)     (AS_OBJ(value)->as<ObjClass>())
#define AS_INSTANCE(value)  (AS_OBJ(value)->as<ObjInstance>())
#define AS_BOUND(value)     (AS_OBJ(value)->as<ObjBoundMethod>())

template<typename T>
inline T* as(Obj* obj) { return dynamic_cast<T*>(obj); }
