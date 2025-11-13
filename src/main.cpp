#include "vm/vm.hpp"
#include <fstream>
#include <sstream>

int main(int argc, char* argv[]) {
    VM vm;

    if (argc == 1) {
        std::string line;
        while (true) {
            std::cout << "> ";
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;
            vm.interpret(line);
        }
    } else if (argc == 2) {
        std::ifstream file(argv[1]);
        std::stringstream buffer;
        buffer << file.rdbuf();
        vm.interpret(buffer.str());
    } else {
        std::cerr << "Usage: intercpp [path]\n";
        return 64;
    }
    return 0;
}
