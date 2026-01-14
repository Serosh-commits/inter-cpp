#include "parser.hpp"
#include "../common/debug.hpp"
#include "../vm/object/string.hpp"
#include "../vm/object/function.hpp"
#include <cstring>

Parser::Parser(VM& v, const std::string& src) : vm(v), scanner(src) {
    advance();
}

ObjFunction* Parser::compile() {
    compiling = vm.newFunction();
    locals.push_back({"", 0, true});
    while (!match(TokenType::TOKEN_EOF)) {
        declaration();
    }
    endCompiler();
    return hadError ? nullptr : compiling;
}

void Parser::advance() {
    previous = current;
    for (;;) {
        current = scanner.scanToken();
        if (current.type != TokenType::ERROR) break;
        errorAtCurrent(current.start);
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
    if (token.type == TokenType::TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token.type != TokenType::ERROR) {
        fprintf(stderr, " at '%.*s'", token.length, token.start);
    }
    fprintf(stderr, ": %s\n", message);
    hadError = true;
}

void Parser::synchronize() {
    panicMode = false;
    while (current.type != TokenType::TOKEN_EOF) {
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
    uint8_t nameConstant = makeConstant(Value(vm.allocateString(std::string(className.start, className.length))));
    emitBytes(static_cast<uint8_t>(OpCode::CLASS), nameConstant);

    defineMethod(vm.allocateString(std::string(className.start, className.length)));
    
    ClassCompiler classCompiler;
    classCompiler.enclosing = this->classCompiler;
    this->classCompiler = &classCompiler;

    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        // variable(false);
        if (className.length == previous.length && memcmp(className.start, previous.start, className.length) == 0) {
            error("A class cannot inherit from itself.");
        }
        beginScope();
        // addLocal(syntheticToken("super"));
        emitBytes(static_cast<uint8_t>(OpCode::GET_GLOBAL), makeConstant(Value(vm.allocateString(std::string(previous.start, previous.length)))));
        this->classCompiler->hasSuperclass = true;
    }

    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::TOKEN_EOF)) {
        funDeclaration("method");
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");

    if (this->classCompiler->hasSuperclass) {
        endScope();
    }

    this->classCompiler = classCompiler.enclosing;
}

void Parser::funDeclaration(const std::string& kind) {
    uint8_t global = 0; // simplified
    consume(TokenType::IDENTIFIER, ("Expect " + kind + " name.").c_str());
    
    std::string name(previous.start, previous.length);
    global = makeConstant(Value(vm.allocateString(name)));

    ObjFunction* function = vm.newFunction();
    // function->name = vm.allocateString(name);
    
    consume(TokenType::LEFT_PAREN, ("Expect '(' after " + kind + " name.").c_str());
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TokenType::LEFT_BRACE, ("Expect '{' before " + kind + " body.").c_str());
    block();

    // emitBytes(static_cast<uint8_t>(OpCode::CLOSURE), global);
}

void Parser::varDeclaration() {
    consume(TokenType::IDENTIFIER, "Expect variable name.");
    Token nameToken = previous;

    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        emitByte(static_cast<uint8_t>(OpCode::NIL));
    }
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

    // defineVariable(global);
}

void Parser::statement() {
    if (match(TokenType::PRINT)) {
        printStatement();
    } else if (match(TokenType::FOR)) {
        forStatement();
    } else if (match(TokenType::IF)) {
        ifStatement();
    } else if (match(TokenType::RETURN)) {
        returnStatement();
    } else if (match(TokenType::WHILE)) {
        whileStatement();
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
    emitByte(static_cast<uint8_t>(OpCode::PRINT));
}

void Parser::expressionStatement() {
    expression();
    consume(TokenType::SEMICOLON, "Expect ';' after expression.");
    emitByte(static_cast<uint8_t>(OpCode::POP));
}

void Parser::ifStatement() {
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    emitByte(static_cast<uint8_t>(OpCode::POP));
    statement();

    int elseJump = emitJump(static_cast<uint8_t>(OpCode::JUMP));
    patchJump(thenJump);
    emitByte(static_cast<uint8_t>(OpCode::POP));

    if (match(TokenType::ELSE)) statement();
    patchJump(elseJump);
}

void Parser::whileStatement() {
    int loopStart = currentChunk()->code.size();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    emitByte(static_cast<uint8_t>(OpCode::POP));
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::POP));
}

void Parser::forStatement() {
    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(TokenType::SEMICOLON)) {
        // No initializer.
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
        exitJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::POP));
    }

    if (!match(TokenType::RIGHT_PAREN)) {
        int bodyJump = emitJump(static_cast<uint8_t>(OpCode::JUMP));
        int incrementStart = currentChunk()->code.size();
        expression();
        emitByte(static_cast<uint8_t>(OpCode::POP));
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::POP));
    }

    endScope();
}

void Parser::returnStatement() {
    if (match(TokenType::SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        emitByte(static_cast<uint8_t>(OpCode::RETURN));
    }
}

void Parser::block() {
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::TOKEN_EOF)) {
        declaration();
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
}

void Parser::expression() {
    parsePrecedence(Precedence::ASSIGNMENT);
}

void Parser::parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(previous.type)->prefix;
    if (prefixRule == nullptr) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= Precedence::ASSIGNMENT;
    (this->*prefixRule)(canAssign);

    while (precedence <= getRule(current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(previous.type)->infix;
        (this->*infixRule)(canAssign);
    }

    if (canAssign && match(TokenType::EQUAL)) {
        error("Invalid assignment target.");
    }
}

Parser::ParseRule* Parser::getRule(TokenType type) {
    static ParseRule rules[] = {
        {&Parser::grouping, &Parser::call, Precedence::CALL},       // LEFT_PAREN
        {nullptr, nullptr, Precedence::NONE},       // RIGHT_PAREN
        {nullptr, nullptr, Precedence::NONE},       // LEFT_BRACE
        {nullptr, nullptr, Precedence::NONE},       // RIGHT_BRACE
        {nullptr, nullptr, Precedence::NONE},       // COMMA
        {nullptr, &Parser::dot, Precedence::CALL},       // DOT
        {&Parser::unary, &Parser::binary, Precedence::TERM},       // MINUS
        {nullptr, &Parser::binary, Precedence::TERM},       // PLUS
        {nullptr, &Parser::binary, Precedence::FACTOR},       // SLASH
        {nullptr, &Parser::binary, Precedence::FACTOR},       // STAR
        {nullptr, nullptr, Precedence::NONE},       // SEMICOLON
        {&Parser::unary, nullptr, Precedence::NONE},       // BANG
        {nullptr, &Parser::binary, Precedence::EQUALITY},       // BANG_EQUAL
        {nullptr, nullptr, Precedence::NONE},       // EQUAL
        {nullptr, &Parser::binary, Precedence::EQUALITY},       // EQUAL_EQUAL
        {nullptr, &Parser::binary, Precedence::COMPARISON},       // GREATER
        {nullptr, &Parser::binary, Precedence::COMPARISON},       // GREATER_EQUAL
        {nullptr, &Parser::binary, Precedence::COMPARISON},       // LESS
        {nullptr, &Parser::binary, Precedence::COMPARISON},       // LESS_EQUAL
        {&Parser::variable, nullptr, Precedence::NONE},       // IDENTIFIER
        {&Parser::string, nullptr, Precedence::NONE},       // STRING
        {&Parser::number, nullptr, Precedence::NONE},       // NUMBER
        {nullptr, &Parser::and_, Precedence::AND},       // AND
        {nullptr, nullptr, Precedence::NONE},       // CLASS
        {nullptr, nullptr, Precedence::NONE},       // ELSE
        {&Parser::literal, nullptr, Precedence::NONE},       // FALSE
        {nullptr, nullptr, Precedence::NONE},       // FUN
        {nullptr, nullptr, Precedence::NONE},       // FOR
        {nullptr, nullptr, Precedence::NONE},       // IF
        {&Parser::literal, nullptr, Precedence::NONE},       // NIL
        {nullptr, &Parser::or_, Precedence::OR},       // OR
        {nullptr, nullptr, Precedence::NONE},       // PRINT
        {nullptr, nullptr, Precedence::NONE},       // RETURN
        {&Parser::super_, nullptr, Precedence::NONE},       // SUPER
        {&Parser::this_, nullptr, Precedence::NONE},       // THIS
        {&Parser::literal, nullptr, Precedence::NONE},       // TRUE
        {nullptr, nullptr, Precedence::NONE},       // VAR
        {nullptr, nullptr, Precedence::NONE},       // WHILE
        {nullptr, nullptr, Precedence::NONE},       // ERROR
        {nullptr, nullptr, Precedence::NONE},       // TOKEN_EOF
    };
    return &rules[static_cast<int>(type)];
}

void Parser::grouping(bool canAssign) {
    expression();
    consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
}

void Parser::unary(bool canAssign) {
    TokenType operatorType = previous.type;
    parsePrecedence(Precedence::UNARY);
    switch (operatorType) {
        case TokenType::MINUS: emitByte(static_cast<uint8_t>(OpCode::NEGATE)); break;
        case TokenType::BANG:  emitByte(static_cast<uint8_t>(OpCode::NOT)); break;
        default: return; 
    }
}

void Parser::binary(bool canAssign) {
    TokenType operatorType = previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence(static_cast<Precedence>(static_cast<int>(rule->precedence) + 1));

    switch (operatorType) {
        case TokenType::BANG_EQUAL:    emitBytes(static_cast<uint8_t>(OpCode::EQUAL), static_cast<uint8_t>(OpCode::NOT)); break;
        case TokenType::EQUAL_EQUAL:   emitByte(static_cast<uint8_t>(OpCode::EQUAL)); break;
        case TokenType::GREATER:       emitByte(static_cast<uint8_t>(OpCode::GREATER)); break;
        case TokenType::GREATER_EQUAL: emitBytes(static_cast<uint8_t>(OpCode::LESS), static_cast<uint8_t>(OpCode::NOT)); break;
        case TokenType::LESS:          emitByte(static_cast<uint8_t>(OpCode::LESS)); break;
        case TokenType::LESS_EQUAL:    emitBytes(static_cast<uint8_t>(OpCode::GREATER), static_cast<uint8_t>(OpCode::NOT)); break;
        case TokenType::PLUS:          emitByte(static_cast<uint8_t>(OpCode::ADD)); break;
        case TokenType::MINUS:         emitByte(static_cast<uint8_t>(OpCode::SUBTRACT)); break;
        case TokenType::STAR:          emitByte(static_cast<uint8_t>(OpCode::MULTIPLY)); break;
        case TokenType::SLASH:         emitByte(static_cast<uint8_t>(OpCode::DIVIDE)); break;
        default: return;
    }
}

void Parser::number(bool canAssign) {
    double value = std::stod(std::string(previous.start, previous.length));
    emitConstant(Value(value));
}

void Parser::literal(bool canAssign) {
    switch (previous.type) {
        case TokenType::FALSE: emitByte(static_cast<uint8_t>(OpCode::FALSE)); break;
        case TokenType::NIL:   emitByte(static_cast<uint8_t>(OpCode::NIL)); break;
        case TokenType::TRUE:  emitByte(static_cast<uint8_t>(OpCode::TRUE)); break;
        default: return;
    }
}

void Parser::string(bool canAssign) {
    emitConstant(Value(vm.allocateString(std::string(previous.start + 1, previous.length - 2))));
}

void Parser::variable(bool canAssign) {
    namedVariable(previous, canAssign);
}

void Parser::namedVariable(const Token& name, bool canAssign) {
    // simplified
    uint8_t arg = 0; 
    int local = resolveLocal(std::string(name.start, name.length));
    OpCode getOp, setOp;
    if (local != -1) {
        getOp = OpCode::GET_LOCAL;
        setOp = OpCode::SET_LOCAL;
        arg = (uint8_t)local;
    } else {
        arg = makeConstant(Value(vm.allocateString(std::string(name.start, name.length))));
        getOp = OpCode::GET_GLOBAL;
        setOp = OpCode::SET_GLOBAL;
    }

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes(static_cast<uint8_t>(setOp), arg);
    } else {
        emitBytes(static_cast<uint8_t>(getOp), arg);
    }
}

void Parser::and_(bool canAssign) {
    int endJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    emitByte(static_cast<uint8_t>(OpCode::POP));
    parsePrecedence(Precedence::AND);
    patchJump(endJump);
}

void Parser::or_(bool canAssign) {
    int elseJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    int endJump = emitJump(static_cast<uint8_t>(OpCode::JUMP));
    patchJump(elseJump);
    emitByte(static_cast<uint8_t>(OpCode::POP));
    parsePrecedence(Precedence::OR);
    patchJump(endJump);
}

void Parser::call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(static_cast<uint8_t>(OpCode::CALL), argCount);
}

void Parser::dot(bool canAssign) {
    consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = makeConstant(Value(vm.allocateString(std::string(previous.start, previous.length))));

    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitBytes(static_cast<uint8_t>(OpCode::SET_PROPERTY), name);
    } else if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        emitBytes(static_cast<uint8_t>(OpCode::INVOKE), name);
        emitByte(argCount);
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::GET_PROPERTY), name);
    }
}

void Parser::this_(bool canAssign) {
    // simplified
}

void Parser::super_(bool canAssign) {
    // simplified
}

int Parser::resolveLocal(const std::string& name) {
    for (int i = locals.size() - 1; i >= 0; i--) {
        if (locals[i].name == name) {
            return i;
        }
    }
    return -1;
}

uint8_t Parser::argumentList() {
    uint8_t argCount = 0;
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
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
    emitByte(static_cast<uint8_t>(OpCode::NIL));
    emitByte(static_cast<uint8_t>(OpCode::RETURN));
}

int Parser::emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->code.size() - 2;
}

void Parser::patchJump(int offset) {
    int jump = currentChunk()->code.size() - offset - 2;
    if (jump > UINT16_MAX) {
        error("Too much code to jump over.");
    }
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

void Parser::emitLoop(int loopStart) {
    emitByte(static_cast<uint8_t>(OpCode::LOOP));
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

void Parser::emitConstant(Value value) {
    emitBytes(static_cast<uint8_t>(OpCode::CONSTANT), makeConstant(value));
}

void Parser::beginScope() {
    scopeDepth++;
}

void Parser::endScope() {
    scopeDepth--;
    while (locals.size() > 0 && locals.back().depth > scopeDepth) {
        emitByte(static_cast<uint8_t>(OpCode::POP));
        locals.pop_back();
    }
}

void Parser::defineMethod(ObjString* name) {
    uint8_t constant = makeConstant(Value(name));
    emitBytes(static_cast<uint8_t>(OpCode::METHOD), constant);
}

void Parser::endCompiler() {
    emitReturn();
}
