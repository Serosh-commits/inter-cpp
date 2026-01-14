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
class ObjNative;

class Obj {
public:
    enum class Type { STRING, FUNCTION, CLOSURE, UPVALUE, CLASS, INSTANCE, BOUND_METHOD, NATIVE };
    Type type;
    bool marked = false;
    Obj* next = nullptr;

    explicit Obj(Type t) : type(t) {}
    virtual ~Obj() = default;
};

#define AS_OBJ(value)       (std::get<Obj*>(value))
#define AS_STRING(value)    (as<ObjString>(AS_OBJ(value)))
#define AS_CSTRING(value)   (AS_STRING(value)->str.c_str())
#define AS_FUNCTION(value)  (as<ObjFunction>(AS_OBJ(value)))
#define AS_CLOSURE(value)   (as<ObjClosure>(AS_OBJ(value)))
#define AS_CLASS(value)     (as<ObjClass>(AS_OBJ(value)))
#define AS_INSTANCE(value)  (as<ObjInstance>(AS_OBJ(value)))
#define AS_BOUND(value)     (as<ObjBoundMethod>(AS_OBJ(value)))
#define AS_NATIVE(value)    (as<ObjNative>(AS_OBJ(value)))

template<typename T>
inline T* as(Obj* obj) { return dynamic_cast<T*>(obj); }
