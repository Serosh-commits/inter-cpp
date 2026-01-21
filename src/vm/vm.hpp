#pragma once
#include "chunk.hpp"
#include "object/object.hpp"
#include "value.hpp"
#include <array>
#include <functional>

using NativeFn = std::function<Value(VM&, const std::vector<Value>&)>;

class CallFrame {
public:
    ObjClosure* closure = nullptr;
    const uint8_t* ip = nullptr;
    Value* slots = nullptr;
};

class VM {
public:
    static constexpr int FRAMES_MAX = 64;
    static constexpr int STACK_MAX = FRAMES_MAX * 256;

    std::array<CallFrame, FRAMES_MAX> frames;
    int frameCount = 0;

    std::array<Value, STACK_MAX> stack;
    Value* stackTop = stack.data();

    std::unordered_map<std::string, Value> globals;
    Obj* objects = nullptr;
    ObjUpvalue* openUpvalues = nullptr;

    size_t bytesAllocated = 0;
    size_t nextGC = 1024 * 1024;

    VM();
    ~VM();

    bool interpret(const std::string& source);
    void runtimeError(const char* format, ...);

    ObjString* allocateString(std::string s);
    ObjFunction* newFunction();
    ObjClosure* newClosure(ObjFunction* function);
    ObjUpvalue* newUpvalue(Value* slot);
    ObjClass* newClass(ObjString* name);
    ObjInstance* newInstance(ObjClass* klass);
    ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
    ObjNative* newNative(NativeFn function, int arity);
    ObjList* newList();

    void push(Value value) { *stackTop++ = value; }
    Value pop() { return *--stackTop; }
    Value peek(int distance) const { return stackTop[-1 - distance]; }

private:
    bool run();
    bool call(ObjClosure* closure, int argCount);
    bool callValue(Value callee, int argCount);
    bool invoke(ObjString* name, int argCount);
    bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount);
    bool bindMethod(ObjClass* klass, const std::string& name);
    ObjUpvalue* captureUpvalue(Value* local);
    void closeUpvalues(Value* last);
    void defineMethod(ObjString* name);

    void defineNative(const std::string& name, int arity, Value (*fn)(VM&, const std::vector<Value>&));

    std::vector<Obj*> grayStack;

    void markRoots();
    void traceReferences();
    void sweep();
    void markObject(Obj* object);
    void blackenObject(Obj* object);
    void markValue(const Value& value);
    void freeObject(Obj* object);
    void freeObjects();

    static Value clockNative(VM&, const std::vector<Value>&);
    static Value inputNative(VM&, const std::vector<Value>&);
};
