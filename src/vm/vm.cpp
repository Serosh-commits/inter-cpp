#include "vm.hpp"
#include "../common/debug.hpp"
#include "object/string.hpp"
#include "object/function.hpp"
#include "object/closure.hpp"
#include "object/upvalue.hpp"
#include "object/class.hpp"
#include "object/instance.hpp"
#include "object/bound_method.hpp"
#include "object/native.hpp"
#include "object/list.hpp"
#include "../compiler/parser.hpp"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <cmath>

VM::VM() {
    defineNative("clock", 0, clockNative);
}

VM::~VM() {
    freeObjects();
}

bool VM::interpret(const std::string& source) {
    Parser parser(*this, source);
    ObjFunction* function = parser.compile();
    if (!function || parser.hadError) return false;

    push(Value(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(Value(closure));
    CallFrame* frame = &frames[frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code.data();
    frame->slots = stack.data();

    return run();
}

void VM::runtimeError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    for (int i = frameCount - 1; i >= 0; --i) {
        CallFrame* frame = &frames[i];
        ObjFunction* func = frame->closure->function;
        size_t instruction = frame->ip - func->chunk.code.data() - 1;
        int line = func->chunk.getLine(instruction);
        fprintf(stderr, "[line %d] in %s\n", line,
                func->name ? func->name->str.c_str() : "script");
    }
    frameCount = 0;
    stackTop = stack.data();
}

bool VM::run() {
    CallFrame* frame = &frames[frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(op) \
    do { \
        if (!std::holds_alternative<double>(peek(0)) || !std::holds_alternative<double>(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return false; \
        } \
        double b = std::get<double>(pop()); \
        double a = std::get<double>(pop()); \
        push(Value(a op b)); \
    } while (false)
#define BITWISE_OP(op) \
    do { \
        if (!std::holds_alternative<double>(peek(0)) || !std::holds_alternative<double>(peek(1))) { \
            runtimeError("Operands must be numbers."); \
            return false; \
        } \
        int b = static_cast<int>(std::get<double>(pop())); \
        int a = static_cast<int>(std::get<double>(pop())); \
        push(Value(static_cast<double>(a op b))); \
    } while (false)

    for (;;) {
        if (bytesAllocated > nextGC) {
            markRoots();
            traceReferences();
            sweep();
            nextGC = bytesAllocated * 2;
        }

        uint8_t instruction = READ_BYTE();
        switch (static_cast<OpCode>(instruction)) {
            case OpCode::CONSTANT: {
                Value constant = READ_CONSTANT();
                push(constant);
                break;
            }
            case OpCode::NIL:   push(Value(nullptr)); break;
            case OpCode::TRUE:  push(Value(true)); break;
            case OpCode::FALSE: push(Value(false)); break;

            case OpCode::ADD: {
                if (isObjType(peek(0), Obj::Type::STRING) && isObjType(peek(1), Obj::Type::STRING)) {
                    ObjString* b = AS_STRING(pop());
                    ObjString* a = AS_STRING(pop());
                    push(Value(allocateString(a->str + b->str)));
                } else if (std::holds_alternative<double>(peek(0)) && std::holds_alternative<double>(peek(1))) {
                    BINARY_OP(+);
                } else {
                    runtimeError("Operands must be two numbers or two strings.");
                    return false;
                }
                break;
            }
            case OpCode::SUBTRACT: BINARY_OP(-); break;
            case OpCode::MULTIPLY: BINARY_OP(*); break;
            case OpCode::DIVIDE:   BINARY_OP(/); break;
            case OpCode::MODULO: {
                if (!std::holds_alternative<double>(peek(0)) || !std::holds_alternative<double>(peek(1))) {
                    runtimeError("Operands must be numbers.");
                    return false;
                }
                double b = std::get<double>(pop());
                double a = std::get<double>(pop());
                push(Value(fmod(a, b)));
                break;
            }
            case OpCode::POW: {
                if (!std::holds_alternative<double>(peek(0)) || !std::holds_alternative<double>(peek(1))) {
                    runtimeError("Operands must be numbers.");
                    return false;
                }
                double b = std::get<double>(pop());
                double a = std::get<double>(pop());
                push(Value(pow(a, b)));
                break;
            }
            case OpCode::BIT_AND: BITWISE_OP(&); break;
            case OpCode::BIT_OR:  BITWISE_OP(|); break;
            case OpCode::BIT_XOR: BITWISE_OP(^); break;
            case OpCode::SHIFT_LEFT: BITWISE_OP(<<); break;
            case OpCode::SHIFT_RIGHT: BITWISE_OP(>>); break;
            case OpCode::BIT_NOT:
                if (!std::holds_alternative<double>(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return false;
                }
                push(Value(static_cast<double>(~static_cast<int>(std::get<double>(pop())))));
                break;
            case OpCode::NOT:
                push(Value(isFalsey(pop())));
                break;
            case OpCode::NEGATE:
                if (!std::holds_alternative<double>(peek(0))) {
                    runtimeError("Operand must be a number.");
                    return false;
                }
                push(Value(-std::get<double>(pop())));
                break;
            case OpCode::EQUAL: {
                Value b = pop();
                Value a = pop();
                push(Value(valuesEqual(a, b)));
                break;
            }
            case OpCode::GREATER:  BINARY_OP(>); break;
            case OpCode::LESS:     BINARY_OP(<); break;

            case OpCode::PRINT: {
                std::cout << valueToString(pop()) << "\n";
                break;
            }
            case OpCode::POP: pop(); break;

            case OpCode::DEFINE_GLOBAL: {
                std::string name = READ_STRING()->str;
                globals[name] = peek(0);
                pop();
                break;
            }
            case OpCode::GET_GLOBAL: {
                std::string name = READ_STRING()->str;
                auto it = globals.find(name);
                if (it == globals.end()) {
                    runtimeError("Undefined variable '%s'.", name.c_str());
                    return false;
                }
                push(it->second);
                break;
            }
            case OpCode::SET_GLOBAL: {
                std::string name = READ_STRING()->str;
                if (globals.find(name) == globals.end()) {
                    runtimeError("Undefined variable '%s'.", name.c_str());
                    return false;
                }
                globals[name] = peek(0);
                break;
            }
            case OpCode::GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(frame->slots[slot]);
                break;
            }
            case OpCode::SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                frame->slots[slot] = peek(0);
                break;
            }
            case OpCode::JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(0))) frame->ip += offset;
                break;
            }
            case OpCode::JUMP: {
                uint16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            case OpCode::LOOP: {
                uint16_t offset = READ_SHORT();
                frame->ip -= offset;
                break;
            }
            case OpCode::CALL: {
                int argCount = READ_BYTE();
                if (!callValue(peek(argCount), argCount)) return false;
                frame = &frames[frameCount - 1];
                break;
            }
            case OpCode::CLOSURE: {
                ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
                ObjClosure* closure = newClosure(function);
                push(Value(closure));
                for (int i = 0; i < closure->upvalues.size(); ++i) {
                    uint8_t isLocal = READ_BYTE();
                    uint8_t index = READ_BYTE();
                    if (isLocal) {
                        closure->upvalues[i] = captureUpvalue(frame->slots + index);
                    } else {
                        closure->upvalues[i] = frame->closure->upvalues[index];
                    }
                }
                break;
            }
            case OpCode::GET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                push(*frame->closure->upvalues[slot]->location);
                break;
            }
            case OpCode::SET_UPVALUE: {
                uint8_t slot = READ_BYTE();
                *frame->closure->upvalues[slot]->location = peek(0);
                break;
            }
            case OpCode::CLOSE_UPVALUE:
                closeUpvalues(stackTop - 1);
                pop();
                break;
            case OpCode::CLASS:
                push(Value(newClass(READ_STRING())));
                break;
            case OpCode::SET_PROPERTY: {
                if (!isObjType(peek(1), Obj::Type::INSTANCE)) {
                    runtimeError("Only instances have properties.");
                    return false;
                }
                ObjInstance* instance = AS_INSTANCE(peek(1));
                instance->fields[READ_STRING()->str] = peek(0);
                Value value = pop();
                pop();
                push(value);
                break;
            }
            case OpCode::GET_PROPERTY: {
                if (!isObjType(peek(0), Obj::Type::INSTANCE)) {
                    runtimeError("Only instances have properties.");
                    return false;
                }
                ObjInstance* instance = AS_INSTANCE(peek(0));
                std::string name = READ_STRING()->str;
                auto it = instance->fields.find(name);
                if (it != instance->fields.end()) {
                    pop();
                    push(it->second);
                    break;
                }
                if (!bindMethod(instance->klass, name)) return false;
                break;
            }
            case OpCode::METHOD:
                defineMethod(READ_STRING());
                break;
            case OpCode::INVOKE: {
                ObjString* method = READ_STRING();
                int argCount = READ_BYTE();
                if (!invoke(method, argCount)) return false;
                frame = &frames[frameCount - 1];
                break;
            }
            case OpCode::INHERIT: {
                ObjClass* superclass = AS_CLASS(peek(1));
                ObjClass* subclass = AS_CLASS(peek(0));
                for (auto& pair : superclass->methods) {
                    subclass->methods[pair.first] = pair.second;
                }
                pop();
                break;
            }
            case OpCode::GET_SUPER: {
                ObjString* name = READ_STRING();
                ObjClass* superclass = AS_CLASS(pop());
                if (!bindMethod(superclass, name->str)) return false;
                break;
            }
            case OpCode::BUILD_LIST: {
                int count = READ_BYTE();
                ObjList* list = newList();
                list->elements.resize(count);
                for (int i = count - 1; i >= 0; i--) {
                    list->elements[i] = pop();
                }
                push(Value(static_cast<Obj*>(list)));
                break;
            }
            case OpCode::GET_SUBSCRIPT: {
                Value index = pop();
                Value listVal = pop();
                if (!isObjType(listVal, Obj::Type::LIST)) {
                    runtimeError("Can only subscript lists.");
                    return false;
                }
                if (!std::holds_alternative<double>(index)) {
                    runtimeError("Index must be a number.");
                    return false;
                }
                ObjList* list = AS_LIST(listVal);
                int i = static_cast<int>(std::get<double>(index));
                if (i < 0 || i >= static_cast<int>(list->elements.size())) {
                    runtimeError("Index out of bounds.");
                    return false;
                }
                push(list->elements[i]);
                break;
            }
            case OpCode::SET_SUBSCRIPT: {
                Value value = pop();
                Value index = pop();
                Value listVal = pop();
                if (!isObjType(listVal, Obj::Type::LIST)) {
                    runtimeError("Can only subscript lists.");
                    return false;
                }
                if (!std::holds_alternative<double>(index)) {
                    runtimeError("Index must be a number.");
                    return false;
                }
                ObjList* list = AS_LIST(listVal);
                int i = static_cast<int>(std::get<double>(index));
                if (i < 0 || i >= static_cast<int>(list->elements.size())) {
                    runtimeError("Index out of bounds.");
                    return false;
                }
                list->elements[i] = value;
                push(value);
                break;
            }
            case OpCode::RETURN: {
                Value result = pop();
                closeUpvalues(frame->slots);
                frameCount--;
                if (frameCount == 0) {
                    pop();
                    return true;
                }
                stackTop = frame->slots;
                push(result);
                frame = &frames[frameCount - 1];
                break;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
#undef BITWISE_OP
}

void VM::defineNative(const std::string& name, int arity, Value (*fn)(VM&, const std::vector<Value>&)) {
    globals[name] = Value(newNative(fn, arity));
}

Value VM::clockNative(VM&, const std::vector<Value>&) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return Value(std::chrono::duration<double>(now).count());
}

ObjString* VM::allocateString(std::string s) {
    ObjString* string = new ObjString(std::move(s));
    string->next = objects;
    objects = string;
    bytesAllocated += sizeof(ObjString) + string->str.capacity();
    return string;
}

ObjFunction* VM::newFunction() {
    ObjFunction* function = new ObjFunction();
    function->next = objects;
    objects = function;
    bytesAllocated += sizeof(ObjFunction);
    return function;
}

ObjClosure* VM::newClosure(ObjFunction* function) {
    ObjClosure* closure = new ObjClosure(function);
    closure->next = objects;
    objects = closure;
    bytesAllocated += sizeof(ObjClosure) + sizeof(ObjUpvalue*) * function->upvalueCount;
    return closure;
}

ObjUpvalue* VM::newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = new ObjUpvalue(slot);
    upvalue->Obj::next = objects;
    objects = upvalue;
    bytesAllocated += sizeof(ObjUpvalue);
    return upvalue;
}

ObjClass* VM::newClass(ObjString* name) {
    ObjClass* klass = new ObjClass(name);
    klass->next = objects;
    objects = klass;
    bytesAllocated += sizeof(ObjClass);
    return klass;
}

ObjInstance* VM::newInstance(ObjClass* klass) {
    ObjInstance* instance = new ObjInstance(klass);
    instance->next = objects;
    objects = instance;
    bytesAllocated += sizeof(ObjInstance);
    return instance;
}

ObjBoundMethod* VM::newBoundMethod(Value receiver, ObjClosure* method) {
    ObjBoundMethod* bound = new ObjBoundMethod(receiver, method);
    bound->next = objects;
    objects = bound;
    bytesAllocated += sizeof(ObjBoundMethod);
    return bound;
}

ObjNative* VM::newNative(NativeFn function, int arity) {
    ObjNative* native = new ObjNative(function, arity);
    native->next = objects;
    objects = native;
    bytesAllocated += sizeof(ObjNative);
    return native;
}

ObjList* VM::newList() {
    ObjList* list = new ObjList();
    list->next = objects;
    objects = list;
    bytesAllocated += sizeof(ObjList);
    return list;
}

void VM::markRoots() {
    for (Value* slot = stack.data(); slot < stackTop; ++slot) {
        markValue(*slot);
    }
    for (int i = 0; i < frameCount; ++i) {
        markObject(reinterpret_cast<Obj*>(frames[i].closure));
    }
    for (ObjUpvalue* upvalue = openUpvalues; upvalue != nullptr; upvalue = upvalue->next) {
        markObject(reinterpret_cast<Obj*>(upvalue));
    }
    for (auto& pair : globals) {
        markValue(pair.second);
    }
}

void VM::traceReferences() {
    while (grayStack.size() > 0) {
        Obj* object = grayStack.back();
        grayStack.pop_back();
        blackenObject(object);
    }
}

void VM::blackenObject(Obj* object) {
    switch (object->type) {
        case Obj::Type::CLASS: {
            ObjClass* klass = reinterpret_cast<ObjClass*>(object);
            markObject(reinterpret_cast<Obj*>(klass->name));
            for (auto& pair : klass->methods) {
                markValue(pair.second);
            }
            break;
        }
        case Obj::Type::CLOSURE: {
            ObjClosure* closure = reinterpret_cast<ObjClosure*>(object);
            markObject(reinterpret_cast<Obj*>(closure->function));
            for (int i = 0; i < closure->upvalues.size(); ++i) {
                markObject(reinterpret_cast<Obj*>(closure->upvalues[i]));
            }
            break;
        }
        case Obj::Type::FUNCTION: {
            ObjFunction* function = reinterpret_cast<ObjFunction*>(object);
            markObject(reinterpret_cast<Obj*>(function->name));
            for (Value constant : function->chunk.constants) {
                markValue(constant);
            }
            break;
        }
        case Obj::Type::INSTANCE: {
            ObjInstance* instance = reinterpret_cast<ObjInstance*>(object);
            markObject(reinterpret_cast<Obj*>(instance->klass));
            for (auto& pair : instance->fields) {
                markValue(pair.second);
            }
            break;
        }
        case Obj::Type::BOUND_METHOD: {
            ObjBoundMethod* bound = reinterpret_cast<ObjBoundMethod*>(object);
            markValue(bound->receiver);
            markObject(reinterpret_cast<Obj*>(bound->method));
            break;
        }
        case Obj::Type::UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case Obj::Type::NATIVE:
        case Obj::Type::STRING:
            break;
        case Obj::Type::LIST: {
            ObjList* list = reinterpret_cast<ObjList*>(object);
            for (Value& val : list->elements) {
                markValue(val);
            }
            break;
        }
    }
}

void VM::sweep() {
    Obj* previous = nullptr;
    Obj* object = objects;
    while (object != nullptr) {
        if (object->marked) {
            object->marked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != nullptr) {
                previous->next = object;
            } else {
                objects = object;
            }
            freeObject(unreached);
        }
    }
}

void VM::markObject(Obj* object) {
    if (object == nullptr || object->marked) return;
    object->marked = true;
    grayStack.push_back(object);
}

void VM::markValue(const Value& value) {
    if (std::holds_alternative<Obj*>(value)) {
        markObject(std::get<Obj*>(value));
    }
}

void VM::freeObject(Obj* object) {
    switch (object->type) {
        case Obj::Type::STRING: delete static_cast<ObjString*>(object); break;
        case Obj::Type::FUNCTION: delete static_cast<ObjFunction*>(object); break;
        case Obj::Type::CLOSURE: delete static_cast<ObjClosure*>(object); break;
        case Obj::Type::UPVALUE: delete static_cast<ObjUpvalue*>(object); break;
        case Obj::Type::CLASS: delete static_cast<ObjClass*>(object); break;
        case Obj::Type::INSTANCE: delete static_cast<ObjInstance*>(object); break;
        case Obj::Type::BOUND_METHOD: delete static_cast<ObjBoundMethod*>(object); break;
        case Obj::Type::NATIVE: delete static_cast<ObjNative*>(object); break;
        case Obj::Type::LIST: delete static_cast<ObjList*>(object); break;
    }
}

void VM::freeObjects() {
    Obj* object = objects;
    while (object != nullptr) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    grayStack.clear();
}

bool VM::call(ObjClosure* closure, int argCount) {
    if (argCount != closure->function->arity) {
        runtimeError("Expected %d arguments but got %d.", closure->function->arity, argCount);
        return false;
    }

    if (frameCount == FRAMES_MAX) {
        runtimeError("Stack overflow.");
        return false;
    }

    CallFrame* frame = &frames[frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code.data();
    frame->slots = stackTop - argCount - 1;
    return true;
}

bool VM::callValue(Value callee, int argCount) {
    if (std::holds_alternative<Obj*>(callee)) {
        Obj* obj = std::get<Obj*>(callee);
        switch (obj->type) {
            case Obj::Type::BOUND_METHOD: {
                ObjBoundMethod* bound = AS_BOUND(callee);
                stackTop[-argCount - 1] = bound->receiver;
                return call(bound->method, argCount);
            }
            case Obj::Type::CLASS: {
                ObjClass* klass = AS_CLASS(callee);
                stackTop[-argCount - 1] = Value(newInstance(klass));
                auto init = klass->methods.find("init");
                if (init != klass->methods.end()) {
                    return call(AS_CLOSURE(init->second), argCount);
                } else if (argCount != 0) {
                    runtimeError("Expected 0 arguments but got %d.", argCount);
                    return false;
                }
                return true;
            }
            case Obj::Type::CLOSURE:
                return call(AS_CLOSURE(callee), argCount);
            case Obj::Type::NATIVE: {
                ObjNative* native = AS_NATIVE(callee);
                Value result = native->function(*this, std::vector<Value>(stackTop - argCount, stackTop));
                stackTop -= argCount + 1;
                push(result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError("Can only call functions and classes.");
    return false;
}

bool VM::invoke(ObjString* name, int argCount) {
    Value receiver = peek(argCount);
    if (!isObjType(receiver, Obj::Type::INSTANCE)) {
        runtimeError("Only instances have methods.");
        return false;
    }
    ObjInstance* instance = AS_INSTANCE(receiver);

    auto field = instance->fields.find(name->str);
    if (field != instance->fields.end()) {
        stackTop[-argCount - 1] = field->second;
        return callValue(field->second, argCount);
    }

    return invokeFromClass(instance->klass, name, argCount);
}

bool VM::invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
    auto method = klass->methods.find(name->str);
    if (method == klass->methods.end()) {
        runtimeError("Undefined property '%s'.", name->str.c_str());
        return false;
    }
    return call(AS_CLOSURE(method->second), argCount);
}

bool VM::bindMethod(ObjClass* klass, const std::string& name) {
    auto method = klass->methods.find(name);
    if (method == klass->methods.end()) {
        runtimeError("Undefined property '%s'.", name.c_str());
        return false;
    }
    ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method->second));
    pop();
    push(Value(bound));
    return true;
}

ObjUpvalue* VM::captureUpvalue(Value* local) {
    ObjUpvalue* prevUpvalue = nullptr;
    ObjUpvalue* upvalue = openUpvalues;
    while (upvalue != nullptr && upvalue->location > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }
    if (upvalue != nullptr && upvalue->location == local) {
        return upvalue;
    }

    ObjUpvalue* createdUpvalue = newUpvalue(local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == nullptr) {
        openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

void VM::closeUpvalues(Value* last) {
    while (openUpvalues != nullptr && openUpvalues->location >= last) {
        ObjUpvalue* upvalue = openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        openUpvalues = upvalue->next;
    }
}

void VM::defineMethod(ObjString* name) {
    Value method = peek(0);
    ObjClass* klass = AS_CLASS(peek(1));
    klass->methods[name->str] = method;
    pop();
}

std::string valueToString(const Value& value) {
    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(10) << std::get<double>(value);
        std::string s = oss.str();
        s.erase(s.find_last_not_of('0') + 1, std::string::npos);
        if (s.back() == '.') s.pop_back();
        return s;
    }
    if (std::holds_alternative<bool>(value)) return std::get<bool>(value) ? "true" : "false";
    if (std::holds_alternative<std::nullptr_t>(value)) return "nil";
    if (std::holds_alternative<Obj*>(value)) {
        Obj* obj = std::get<Obj*>(value);
        if (obj->type == Obj::Type::STRING) return AS_STRING(value)->str;
        if (obj->type == Obj::Type::FUNCTION) {
            ObjFunction* f = AS_FUNCTION(value);
            return f->name ? "<fn " + f->name->str + ">" : "<script>";
        }
        if (obj->type == Obj::Type::CLASS) return AS_CLASS(value)->name->str;
        if (obj->type == Obj::Type::INSTANCE) return AS_INSTANCE(value)->klass->name->str + " instance";
        if (obj->type == Obj::Type::BOUND_METHOD) return "<bound method>";
        if (obj->type == Obj::Type::NATIVE) return "<native fn>";
        if (obj->type == Obj::Type::LIST) {
            ObjList* list = AS_LIST(value);
            std::string result = "[";
            for (size_t i = 0; i < list->elements.size(); i++) {
                if (i > 0) result += ", ";
                result += valueToString(list->elements[i]);
            }
            result += "]";
            return result;
        }
    }
    return "<object>";
}
