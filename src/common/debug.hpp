#pragma once
#include "../vm/chunk.hpp"
#include "../vm/value.hpp"

void disassembleChunk(const Chunk& chunk, const std::string& name);
int disassembleInstruction(const Chunk& chunk, int offset);
std::string valueToString(const Value& value);
