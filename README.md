# inter-cpp

A high-performance bytecode interpreter written in modern C++17.

This project implements a complete stack-based virtual machine with a recursive-descent + Pratt parser compiler, garbage collection, closures, classes, and inheritance.

---

## Features

- **Zero external dependencies**
- **C++17** with clean, modular design
- **Stack-based VM** with 64 call frames
- **Mark-sweep garbage collector**
- **Closures with upvalue capture**
- **Classes, inheritance, `this`, `super`**
- **REPL and file execution**
- **Native functions** (`clock()` built-in)
- **String interning** and concatenation
- **Error recovery** in parser

---

## Build

```bash
mkdir build && cd build
cmake ..
make -j
