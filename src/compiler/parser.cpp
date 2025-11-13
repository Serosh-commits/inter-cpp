#include "parser.hpp"
#include "../common/debug.hpp"
#include <cstring>

Parser::Parser(VM& v, const std::string& src) : vm(v), scanner(src) {
    advance();
}

ObjFunction* Parser::compile() {
    compiling = vm.newFunction();
    locals.push_back({"", 0, true});
    advance();
    while (!match(TokenType::EOF)) {
        declaration();
    }
    endCompiler();
    return hadError ? nullptr : compiling;
}

void Parser::advance() {
    previous = current;
    for (;;) {
        current = scanner.scanTokens().back();
        if (current.type != TokenType::ERROR) break;
        errorAtCurrent("");
    }
}

void Parser::consume(TokenType type, const char* message) {
    if (current.type == type) {
        advance();
        return;
    }
    errorAtCurrent(message);
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::check(TokenType type) const {
    return current.type == type;
}

void Parser::errorAtCurrent(const char* message) {
    errorAt(current, message);
}

void Parser::error(const char* message) {
    errorAt(previous, message);
}

void Parser::errorAt(const Token& token, const char* message) {
    if (panicMode) return;
    panicMode = true;
    fprintf(stderr, "[line %d] Error", token.line);
    if (token.type == TokenType::EOF) {
        fprintf(stderr, " at end");
    } else if (token.type != TokenType::ERROR) {
        fprintf(stderr, " at '%.*s'", token.length, token.start);
    }
    fprintf(stderr, ": %s\n", message);
    hadError = true;
}

void Parser::synchronize() {
    panicMode = false;
    while (current.type != TokenType::EOF) {
        if (previous.type == TokenType::SEMICOLON) return;
        switch (current.type) {
            case TokenType::CLASS:
            case TokenType::FUN:
            case TokenType::VAR:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::PRINT:
            case TokenType::RETURN:
                return;
            default:
                ;
        }
        advance();
    }
}

void Parser::expression() {
    parsePrecedence(Precedence::ASSIGNMENT);
}

void Parser::declaration() {
    if (match(TokenType::CLASS)) {
        classDeclaration();
    } else if (match(TokenType::FUN)) {
        funDeclaration("function");
    } else if (match(TokenType::VAR)) {
        varDeclaration();
    } else {
        statement();
    }
    if (panicMode) synchronize();
}

void Parser::classDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect class name.");
    Token className = previous;
    uint8_t nameConstant = makeConstant(ObjString::copyString(vm, className.start, className.length));
    emitBytes((uint8_t)OpCode::CONSTANT, nameConstant);

    ClassCompiler classCompiler;
    classCompiler.enclosing = this->classCompiler;
    this->classCompiler = &classCompiler;

    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        variable(false);
        emitByte((uint8_t)OpCode::INHERIT);
        classCompiler.hasSuperclass = true;
    }

    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::EOF)) {
        funDeclaration("method");
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");
    emitByte((uint8_t)OpCode::CLASS);
    this->classCompiler = classCompiler.enclosing;
}

void Parser::funDeclaration(const std::string& kind) {
    uint8_t global = makeConstant(ObjString::copyString(vm, previous.start, previous.length));
    ObjFunction* function = vm.newFunction();
    function->name = ObjString::copyString(vm, previous.start, previous.length);
    compiling = function;
    locals.clear();
    locals.push_back({"", 0, true});
    scopeDepth = 0;
    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after " + kind + " name.");
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            function->arity++;
            uint8_t paramConstant = makeConstant(ObjString::copyString(vm, current.start, current.length));
            consume(TokenType::IDENTIFIER, "Expect parameter name.");
            emitBytes((uint8_t)OpCode::CONSTANT, paramConstant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TokenType::LEFT_BRACE, "Expect '{' before " + kind + " body.");
    block();
    endCompiler();
    emitBytes((uint8_t)OpCode::CLOSURE, makeConstant(function));
}

void Parser::varDeclaration() {
    uint8_t global = makeConstant(ObjString::copyString(vm, previous.start, previous.length));
    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        emitByte((uint8_t)OpCode::NIL);
    }
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
    emitBytes((uint8_t)OpCode::DEFINE_GLOBAL, global);
}

void Parser::statement() {
    if (match(TokenType::PRINT)) {
        printStatement();
    } else if (match(TokenType::IF)) {
        ifStatement();
    } else if (match(TokenType::WHILE)) {
        whileStatement();
    } else if (match(TokenType::FOR)) {
        forStatement();
    } else if (match(TokenType::RETURN)) {
        returnStatement();
    } else if (match(TokenType::LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

void Parser::printStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after value.");
    emitByte((uint8_t)OpCode::PRINT);
}

void Parser::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    emitByte((uint8_t)OpCode::POP);
}

void Parser::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump((uint8_t)OpCode::JUMP_IF_FALSE);
    emitByte((uint8_t)OpCode::POP);
    statement();
    int elseJump = emitJump((uint8_t)OpCode::JUMP);

    patchJump(thenJump);
    emitByte((uint8_t)OpCode::POP);

    if (match(TokenType::ELSE)) statement();
    patchJump(elseJump);
}

void Parser::whileStatement() {
    int loopStart = currentChunk()->code.size();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump((uint8_t)OpCode::JUMP_IF_FALSE);
    emitByte((uint8_t)OpCode::POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte((uint8_t)OpCode::POP);
}

void Parser::forStatement() {
    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TokenType::SEMICOLON)) {
    } else if (match(TokenType::VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }

    int loopStart = currentChunk()->code.size();
    int exitJump = -1;
    if (!match(TokenType::SEMICOLON)) {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump((uint8_t)OpCode::JUMP_IF_FALSE);
        emitByte((uint8_t)OpCode::POP);
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        int bodyJump = emitJump((uint8_t)OpCode::JUMP);
        int incrementStart = currentChunk()->code.size();
        expression();
        emitByte((uint8_t)OpCode::POP);
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte((uint8_t)OpCode::POP);
    }

    endScope();
}

void Parser::returnStatement() {
    if (compiling->name == nullptr) {
        error("Can't return from top-level code.");
    }
    if (match(TokenType::SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        emitByte((uint8_t)OpCode::RETURN);
    }
}

void Parser::block() {
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::EOF)) {
        declaration();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
}

void Parser::parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(previous.type)->prefix;
    if (prefixRule == nullptr) {
        error("Expect expression.");
        return;
    }
    (this->*prefixRule)();

    while (precedence <= getRule(current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(previous.type)->infix;
        (this->*infixRule)();
    }
}

Parser::ParseRule* Parser::getRule(TokenType type) {
    static ParseRule rules[] = {
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {&Parser::dot, &Parser::call, Precedence::CALL},
        {&Parser::unary, &Parser::binary, Precedence::TERM},
        {nullptr, &Parser::binary, Precedence::TERM},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, &Parser::binary, Precedence::FACTOR},
        {nullptr, &Parser::binary, Precedence::FACTOR},
        {&Parser::unary, nullptr, Precedence::NONE},
        {nullptr, &Parser::binary, Precedence::EQUALITY},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, &Parser::binary, Precedence::EQUALITY},
        {nullptr, &Parser::binary, Precedence::COMPARISON},
        {nullptr, &Parser::binary, Precedence::COMPARISON},
        {nullptr, &Parser::binary, Precedence::COMPARISON},
        {nullptr, &Parser::binary, Precedence::COMPARISON},
        {&Parser::variable, nullptr, Precedence::NONE},
        {&Parser::string, nullptr, Precedence::NONE},
        {&Parser::number, nullptr, Precedence::NONE},
        {nullptr, &Parser::and_, Precedence::AND},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {&Parser::literal, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {&Parser::literal, nullptr, Precedence::NONE},
        {nullptr, &Parser::or_, Precedence::OR},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {&Parser::super_, nullptr, Precedence::NONE},
        {&Parser::this_, nullptr, Precedence::NONE},
        {&Parser::literal, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
    };
    return &rules[static_cast<int>(type)];
}

void Parser::grouping() {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Parser::unary() {
    TokenType opType = previous.type;
    parsePrecedence(Precedence::UNARY);
    switch (opType) {
        case TokenType::BANG: emitByte((uint8_t)OpCode::NOT); break;
        case TokenType::MINUS: emitByte((uint8_t)OpCode::NEGATE); break;
        default: return;
    }
}

void Parser::binary() {
    TokenType opType = previous.type;
    ParseRule* rule = getRule(opType);
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule->precedence) + 1));

    switch (opType) {
        case TokenType::BANG_EQUAL: emitBytes((uint8_t)OpCode::EQUAL, (uint8_t)OpCode::NOT); break;
        case TokenType::EQUAL_EQUAL: emitByte((uint8_t)OpCode::EQUAL); break;
        case TokenType::GREATER: emitByte((uint8_t)OpCode::GREATER); break;
        case TokenType::GREATER_EQUAL: emitBytes((uint8_t)OpCode::LESS, (uint8_t)OpCode::NOT); break;
        case TokenType::LESS: emitByte((uint8_t)OpCode::LESS); break;
        case TokenType::LESS_EQUAL: emitBytes((uint8_t)OpCode::GREATER, (uint8_t)OpCode::NOT); break;
        case TokenType::PLUS: emitByte((uint8_t)OpCode::ADD); break;
        case TokenType::MINUS: emitByte((uint8_t)OpCode::SUBTRACT); break;
        case TokenType::STAR: emitByte((uint8_t)OpCode::MULTIPLY); break;
        case TokenType::SLASH: emitByte((uint8_t)OpCode::DIVIDE); break;
        default: return;
    }
}

void Parser::number() {
    double value = strtod(previous.start, nullptr);
    emitConstant(value);
}

void Parser::literal() {
    switch (previous.type) {
        case TokenType::FALSE: emitByte((uint8_t)OpCode::FALSE); break;
        case TokenType::NIL: emitByte((uint8_t)OpCode::NIL); break;
        case TokenType::TRUE: emitByte((uint8_t)OpCode::TRUE); break;
        default: return;
    }
}

void Parser::string() {
    emitConstant(ObjString::copyString(vm, previous.start + 1, previous.length - 2));
}

void Parser::variable(bool canAssign) {
    namedVariable(previous, canAssign);
}

void Parser::namedVariable(const Token& name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(name.toString());
    if (arg != -1) {
        getOp = (uint8_t)OpCode::GET_LOCAL;
        setOp = (uint8_t)OpCode::SET_LOCAL;
    } else {
        arg = makeConstant(ObjString::copyString(vm, name.start, name.length));
        getOp = (uint8_t)OpCode::GET_GLOBAL;
        setOp = (uint8_t)OpCode::SET_GLOBAL;
    }

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

void Parser::and_() {
    int endJump = emitJump((uint8_t)OpCode::JUMP_IF_FALSE);
    emitByte((uint8_t)OpCode::POP);
    parsePrecedence(Precedence::AND);
    patchJump(endJump);
}

void Parser::or_() {
    int elseJump = emitJump((uint8_t)OpCode::JUMP_IF_FALSE);
    int endJump = emitJump((uint8_t)OpCode::JUMP);

    patchJump(elseJump);
    emitByte((uint8_t)OpCode::POP);

    parsePrecedence(Precedence::OR);
    patchJump(endJump);
}

void Parser::call() {
    uint8_t argCount = argumentList();
    emitBytes((uint8_t)OpCode::CALL, argCount);
}

uint8_t Parser::argumentList() {
    uint8_t count = 0;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            expression();
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return count;
}

void Parser::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = makeConstant(ObjString::copyString(vm, previous.start, previous.length));

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes((uint8_t)OpCode::SET_PROPERTY, name);
    } else if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes((uint8_t)OpCode::INVOKE, name);
        emitByte(argCount);
    } else {
        emitBytes((uint8_t)OpCode::GET_PROPERTY, name);
    }
}

void Parser::this_() {
    if (classCompiler == nullptr) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

void Parser::super_() {
    if (classCompiler == nullptr) {
        error("Can't use 'super' outside of a class.");
    } else if (!classCompiler->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TokenType::DOT, "Expect '.' after 'super'.");
    consume(TokenType::IDENTIFIER, "Expect superclass method name.");
    uint8_t name = makeConstant(ObjString::copyString(vm, previous.start, previous.length));

    namedVariable(Token{TokenType::IDENTIFIER, "this", 4, previous.line}, false);
    if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(Token{TokenType::IDENTIFIER, "super", 5, previous.line}, false);
        emitBytes((uint8_t)OpCode::INVOKE, name);
        emitByte(argCount);
    } else {
        namedVariable(Token{TokenType::IDENTIFIER, "super", 5, previous.line}, false);
        emitBytes((uint8_t)OpCode::GET_SUPER, name);
    }
}

Chunk* Parser::currentChunk() {
    return &compiling->chunk;
}

void Parser::emitByte(uint8_t byte) {
    currentChunk()->write(byte, previous.line);
}

void Parser::emitBytes(uint8_t a, uint8_t b) {
    emitByte(a);
    emitByte(b);
}

void Parser::emitReturn() {
    emitByte((uint8_t)OpCode::NIL);
    emitByte((uint8_t)OpCode::RETURN);
}

int Parser::emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->code.size() - 2;
}

void Parser::patchJump(int offset) {
    int jump = currentChunk()->code.size() - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

void Parser::emitLoop(int loopStart) {
    emitByte((uint8_t)OpCode::LOOP);
    int offset = currentChunk()->code.size() - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

int Parser::makeConstant(Value value) {
    int constant = currentChunk()->addConstant(value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return constant;
}

void Parser::endCompiler() {
    emitReturn();
    if (!hadError && DEBUG_PRINT_CODE) {
        disassembleChunk(*currentChunk(), compiling->name ? compiling->name->str : "<script>");
    }
}
