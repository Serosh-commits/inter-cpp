cmake_minimum_required(VERSION 3.14)
project(inter_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(intercpp
    src/main.cpp
    src/vm/vm.cpp
    src/vm/chunk.cpp
    src/vm/object/string.cpp
    src/vm/object/function.cpp
    src/vm/object/closure.cpp
    src/vm/object/upvalue.cpp
    src/vm/object/class.cpp
    src/vm/object/instance.cpp
    src/compiler/scanner.cpp
    src/compiler/parser.cpp
)

target_include_directories(intercpp PRIVATE src)
