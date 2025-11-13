#pragma once
#include "../common/common.hpp"
#include "value.hpp"

enum class OpCode : uint8_t {
    CONSTANT, NIL, TRUE, FALSE,
    ADD, SUBTRACT, MULTIPLY, DIVIDE, NEGATE,
    NOT, EQUAL, GREATER, LESS,
    PRINT, POP, DEFINE_GLOBAL, GET_GLOBAL, SET_GLOBAL,
    GET_LOCAL, SET_LOCAL, JUMP_IF_FALSE, JUMP, LOOP,
    CALL, CLOSURE, GET_UPVALUE, SET_UPVALUE, CLOSE_UPVALUE,
    CLASS, SET_PROPERTY, GET_PROPERTY, INVOKE, INHERIT, GET_SUPER, RETURN
};

class Chunk {
public:
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<int> lines;

    void write(uint8_t byte, int line);
    int addConstant(Value value);
    int getLine(size_t offset) const;
};
