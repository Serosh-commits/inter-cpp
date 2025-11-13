#include "chunk.hpp"

void Chunk::write(uint8_t byte, int line) {
    code.push_back(byte);
    if (lines.empty() || lines.back() != line) {
        lines.push_back(line);
    } else {
        lines.back() = line;
    }
}

int Chunk::addConstant(Value value) {
    constants.push_back(value);
    return static_cast<int>(constants.size()) - 1;
}

int Chunk::getLine(size_t offset) const {
    size_t i = 0;
    for (size_t idx = 0; idx < lines.size(); ++idx) {
        size_t next = (idx + 1 < lines.size()) ? (i + 1) : code.size();
        if (offset < next) return lines[idx];
        i = next;
    }
    return lines.back();
}
