#pragma once
#include "scanner.hpp"
#include "vm/vm.hpp"

class Parser {
public:
    VM& vm;
    Scanner scanner;
    Token current, previous;
    bool hadError = false;
    bool panicMode = false;

    Parser(VM& v, const std::string& source);
    ObjFunction* compile();

private:
    void advance();
    void consume(TokenType type, const char* message);
    bool match(TokenType type);
    bool check(TokenType type) const;
    void errorAtCurrent(const char* message);
    void error(const char* message);
    void errorAt(const Token& token, const char* message);
    void synchronize();

    void expression();
    void statement();
    void declaration();
    void varDeclaration();
    void funDeclaration(const std::string& kind);
    void classDeclaration();
    void printStatement();
    void expressionStatement();
    void ifStatement();
    void whileStatement();
    void forStatement();
    void returnStatement();
    void block();

    enum class Precedence {
        NONE, ASSIGNMENT, OR, AND, EQUALITY, COMPARISON,
        TERM, FACTOR, UNARY, CALL, PRIMARY
    };

    using ParseFn = void (Parser::*)();
    struct ParseRule {
        ParseFn prefix;
        ParseFn infix;
        Precedence precedence;
    };

    void parsePrecedence(Precedence precedence);
    ParseRule* getRule(TokenType type);
    void grouping();
    void unary();
    void binary();
    void number();
    void literal();
    void string();
    void variable(bool canAssign);
    void namedVariable(const Token& name, bool canAssign);
    void and_();
    void or_();
    void call();
    void dot(bool canAssign);
    void this_();
    void super_();

    Chunk* currentChunk();
    void emitByte(uint8_t byte);
    void emitBytes(uint8_t a, uint8_t b);
    void emitReturn();
    int emitJump(uint8_t instruction);
    void patchJump(int offset);
    void emitLoop(int loopStart);
    int makeConstant(Value value);
    void endCompiler();

    ObjFunction* compiling = nullptr;
    struct Local { std::string name; int depth; bool initialized; };
    std::vector<Local> locals;
    int scopeDepth = 0;

    struct Upvalue { uint8_t index; bool isLocal; };
    std::vector<Upvalue> upvalues;

    class ClassCompiler {
    public:
        ClassCompiler* enclosing = nullptr;
        bool hasSuperclass = false;
    };
    ClassCompiler* classCompiler = nullptr;
};
