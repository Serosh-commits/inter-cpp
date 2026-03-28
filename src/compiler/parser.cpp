#include "parser.hpp"
#include "../common/debug.hpp"
#include "../vm/object/string.hpp"
#include "../vm/object/function.hpp"
#include <cstring>

Parser::Parser(VM& v, const std::string& src) : vm(v), scanner(src) {
    advance();
}

ObjFunction* Parser::compile() {
    Compiler compiler;
    initCompiler(&compiler, FunctionType::SCRIPT);
    while (!match(TokenType::TOKEN_EOF)) {
        declaration();
    }
    endCompiler();
    return hadError ? nullptr : compiler.function;
}

void Parser::initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = currentCompiler;
    compiler->function = vm.newFunction();
    compiler->type = type;
    compiler->scopeDepth = 0;
    currentCompiler = compiler;

    if (type != FunctionType::SCRIPT) {
        currentCompiler->function->name = vm.allocateString(std::string(previous.start, previous.length));
    }

    currentCompiler->locals.push_back({(type != FunctionType::FUNCTION ? "this" : ""), 0, true, false});
}

void Parser::endCompiler() {
    emitReturn();
    if (currentCompiler->enclosing != nullptr) {
        currentCompiler = currentCompiler->enclosing;
    }
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
    uint8_t nameConstant = identifierConstant(className);
    declareVariable();

    emitBytes(static_cast<uint8_t>(OpCode::CLASS), nameConstant);
    defineVariable(nameConstant);

    ClassCompiler classCompiler;
    classCompiler.enclosing = this->classCompiler;
    this->classCompiler = &classCompiler;

    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expect superclass name.");
        if (className.length == previous.length && memcmp(className.start, previous.start, className.length) == 0) {
            error("A class cannot inherit from itself.");
        }
        namedVariable(previous, false);
        beginScope();
        currentCompiler->locals.push_back({"super", currentCompiler->scopeDepth, true, false});
        this->classCompiler->hasSuperclass = true;
        
        namedVariable(className, false);
        emitByte(static_cast<uint8_t>(OpCode::INHERIT));
    }

    namedVariable(className, false);

    consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");
    while (!check(TokenType::RIGHT_BRACE) && !check(TokenType::TOKEN_EOF)) {
        funDeclaration("method");
    }
    consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");

    emitByte(static_cast<uint8_t>(OpCode::POP));
    if (this->classCompiler->hasSuperclass) endScope();

    this->classCompiler = classCompiler.enclosing;
}

void Parser::funDeclaration(const std::string& kind) {
    consume(TokenType::IDENTIFIER, ("Expect " + kind + " name.").c_str());
    uint8_t global = identifierConstant(previous);
    if (kind != "method") declareVariable();

    FunctionType type = FunctionType::FUNCTION;
    if (kind == "method") {
        type = (previous.length == 4 && memcmp(previous.start, "init", 4) == 0) ? FunctionType::INITIALIZER : FunctionType::METHOD;
    }

    function(type);

    if (kind != "method") {
        defineVariable(global);
    } else {
        emitBytes(static_cast<uint8_t>(OpCode::METHOD), global);
    }
}

void Parser::function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();
    consume(TokenType::LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            currentCompiler->function->arity++;
            if (currentCompiler->function->arity > 255) errorAtCurrent("Can't have more than 255 parameters.");
            consume(TokenType::IDENTIFIER, "Expect parameter name.");
            uint8_t constant = identifierConstant(previous);
            declareVariable();
            defineVariable(constant);
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TokenType::LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction* f = currentCompiler->function;
    auto ups = currentCompiler->upvalues;
    endCompiler();
    uint8_t constant = makeConstant(Value(f));
    emitBytes(static_cast<uint8_t>(OpCode::CLOSURE), constant);

    for (const auto& up : ups) {
        emitByte(up.isLocal ? 1 : 0);
        emitByte(up.index);
    }
}

void Parser::varDeclaration() {
    uint8_t global = identifierConstant(current);
    advance();
    declareVariable();
    if (match(TokenType::EQUAL)) {
        expression();
    } else {
        emitByte(static_cast<uint8_t>(OpCode::NIL));
    }
    consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
    defineVariable(global);
}

void Parser::declareVariable() {
    if (currentCompiler->scopeDepth == 0) return;
    std::string name(previous.start, previous.length);
    for (int i = currentCompiler->locals.size() - 1; i >= 0; i--) {
        if (currentCompiler->locals[i].depth != -1 && currentCompiler->locals[i].depth < currentCompiler->scopeDepth) break;
        if (name == currentCompiler->locals[i].name) error("Already a variable with this name in this scope.");
    }
    currentCompiler->locals.push_back({name, -1, false, false});
}

void Parser::defineVariable(uint8_t global) {
    if (currentCompiler->scopeDepth > 0) {
        currentCompiler->locals.back().depth = currentCompiler->scopeDepth;
        currentCompiler->locals.back().initialized = true;
        return;
    }
    emitBytes(static_cast<uint8_t>(OpCode::DEFINE_GLOBAL), global);
}

uint8_t Parser::identifierConstant(const Token& name) {
    return makeConstant(Value(vm.allocateString(std::string(name.start, name.length))));
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
    } else if (match(TokenType::BREAK)) {
        breakStatement();
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

    Loop loop = {loopStart, currentCompiler->scopeDepth, {}, currentLoop};
    currentLoop = &loop;

    int exitJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    emitByte(static_cast<uint8_t>(OpCode::POP));
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(static_cast<uint8_t>(OpCode::POP));

    for (int jump : loop.exitJumps) {
        patchJump(jump);
    }
    currentLoop = loop.enclosing;
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
        exitJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
        emitByte(static_cast<uint8_t>(OpCode::POP));
    }

    Loop loop = {loopStart, currentCompiler->scopeDepth, {}, currentLoop};
    currentLoop = &loop;

    if (!match(TokenType::RIGHT_PAREN)) {
        int bodyJump = emitJump(static_cast<uint8_t>(OpCode::JUMP));
        int incrementStart = currentChunk()->code.size();
        expression();
        emitByte(static_cast<uint8_t>(OpCode::POP));
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
        loop.start = loopStart; 
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(static_cast<uint8_t>(OpCode::POP));
    }

    for (int jump : loop.exitJumps) {
        patchJump(jump);
    }
    currentLoop = loop.enclosing;

    endScope();
}

void Parser::breakStatement() {
    if (currentLoop == nullptr) {
        error("Can't use 'break' outside of a loop.");
    }

    consume(TokenType::SEMICOLON, "Expect ';' after 'break'.");

    for (int i = currentCompiler->locals.size() - 1; i >= 0 && currentCompiler->locals[i].depth > currentLoop->scopeDepth; i--) {
        emitByte(static_cast<uint8_t>(OpCode::POP));
    }

    currentLoop->exitJumps.push_back(emitJump(static_cast<uint8_t>(OpCode::JUMP)));
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
        {&Parser::grouping, &Parser::call, Precedence::CALL},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {&Parser::list, &Parser::subscript, Precedence::CALL},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, &Parser::dot, Precedence::CALL},
        {&Parser::unary, &Parser::binary, Precedence::TERM},
        {nullptr, &Parser::binary, Precedence::TERM},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, &Parser::binary, Precedence::FACTOR},
        {nullptr, &Parser::binary, Precedence::FACTOR},
        {nullptr, &Parser::pow, Precedence::INDICES},
        {nullptr, &Parser::binary, Precedence::FACTOR},
        {nullptr, &Parser::binary, Precedence::BIT_AND},
        {nullptr, &Parser::binary, Precedence::BIT_OR},
        {nullptr, &Parser::binary, Precedence::BIT_XOR},
        {&Parser::unary, nullptr, Precedence::NONE},
        {nullptr, &Parser::binary, Precedence::SHIFT},
        {nullptr, &Parser::binary, Precedence::SHIFT},
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
        {&Parser::unary, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, nullptr, Precedence::NONE},
        {nullptr, &Parser::ternary, Precedence::TERNARY},
        {nullptr, nullptr, Precedence::NONE},
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
        case TokenType::TILDE: emitByte(static_cast<uint8_t>(OpCode::BIT_NOT)); break;
        case TokenType::TYPEOF: emitByte(static_cast<uint8_t>(OpCode::TYPEOF)); break;
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
        case TokenType::PERCENT:       emitByte(static_cast<uint8_t>(OpCode::MODULO)); break;
        case TokenType::AMPERSAND:     emitByte(static_cast<uint8_t>(OpCode::BIT_AND)); break;
        case TokenType::PIPE:          emitByte(static_cast<uint8_t>(OpCode::BIT_OR)); break;
        case TokenType::CARET:         emitByte(static_cast<uint8_t>(OpCode::BIT_XOR)); break;
        case TokenType::LESS_LESS:     emitByte(static_cast<uint8_t>(OpCode::SHIFT_LEFT)); break;
        case TokenType::GREATER_GREATER: emitByte(static_cast<uint8_t>(OpCode::SHIFT_RIGHT)); break;
        default: return;
    }
}

void Parser::ternary(bool canAssign) {
    int thenJump = emitJump(static_cast<uint8_t>(OpCode::JUMP_IF_FALSE));
    emitByte(static_cast<uint8_t>(OpCode::POP));
    parsePrecedence(Precedence::TERNARY);

    consume(TokenType::COLON, "Expect ':' after '?' branch.");

    int elseJump = emitJump(static_cast<uint8_t>(OpCode::JUMP));

    patchJump(thenJump);
    emitByte(static_cast<uint8_t>(OpCode::POP));

    parsePrecedence(Precedence::TERNARY);

    patchJump(elseJump);
}

void Parser::pow(bool canAssign) {
    parsePrecedence(Precedence::INDICES);
    emitByte(static_cast<uint8_t>(OpCode::POW));
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
    uint8_t arg = 0;
    int local = resolveLocal(std::string(name.start, name.length));
    OpCode getOp, setOp;
    if (local != -1) {
        getOp = OpCode::GET_LOCAL;
        setOp = OpCode::SET_LOCAL;
        arg = (uint8_t)local;
    } else if ((local = resolveUpvalue(currentCompiler, std::string(name.start, name.length))) != -1) {
        getOp = OpCode::GET_UPVALUE;
        setOp = OpCode::SET_UPVALUE;
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
    if (classCompiler == nullptr) {
        error("Can't use 'this' outside of a class.");
        return;
    }
    namedVariable(previous, false);
}

void Parser::super_(bool canAssign) {
    if (classCompiler == nullptr) {
        error("Can't use 'super' outside of a class.");
    } else if (!classCompiler->hasSuperclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TokenType::DOT, "Expect '.' after 'super'.");
    consume(TokenType::IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifierConstant(previous);

    namedVariable(Token{TokenType::THIS, "this", 4, previous.line}, false);
    if (match(TokenType::LEFT_PAREN)) {
        uint8_t argCount = argumentList();
        namedVariable(Token{TokenType::SUPER, "super", 5, previous.line}, false);
        emitBytes(static_cast<uint8_t>(OpCode::SUPER_INVOKE), name);
        emitByte(argCount);
    } else {
        namedVariable(Token{TokenType::SUPER, "super", 5, previous.line}, false);
        emitBytes(static_cast<uint8_t>(OpCode::GET_SUPER), name);
    }
}

void Parser::list(bool canAssign) {
    int count = 0;
    if (!check(TokenType::RIGHT_BRACKET)) {
        do {
            if (count == 255) {
                error("Can't have more than 255 elements in a list literal.");
            }
            expression();
            count++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after list elements.");
    emitBytes(static_cast<uint8_t>(OpCode::BUILD_LIST), static_cast<uint8_t>(count));
}

void Parser::subscript(bool canAssign) {
    expression();
    consume(TokenType::RIGHT_BRACKET, "Expect ']' after index.");
    if (canAssign && match(TokenType::EQUAL)) {
        expression();
        emitByte(static_cast<uint8_t>(OpCode::SET_SUBSCRIPT));
    } else {
        emitByte(static_cast<uint8_t>(OpCode::GET_SUBSCRIPT));
    }
}

int Parser::resolveLocal(const std::string& name) {
    return resolveLocalInCompiler(currentCompiler, name);
}

int Parser::resolveLocalInCompiler(Compiler* compiler, const std::string& name) {
    for (int i = compiler->locals.size() - 1; i >= 0; i--) {
        if (compiler->locals[i].name == name) {
            if (compiler->locals[i].depth == -1) error("Can't read local variable in its own initializer.");
            return i;
        }
    }
    return -1;
}

int Parser::resolveUpvalue(Compiler* compiler, const std::string& name) {
    if (compiler->enclosing == nullptr) return -1;
    int local = resolveLocalInCompiler(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isUpvalue = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) return addUpvalue(compiler, (uint8_t)upvalue, false);
    return -1;
}

int Parser::addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        if (compiler->upvalues[i].index == index && compiler->upvalues[i].isLocal == isLocal) return i;
    }
    if (upvalueCount == 256) {
        error("Too many closure variables in function.");
        return 0;
    }
    compiler->upvalues.push_back({index, isLocal});
    return compiler->function->upvalueCount++;
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
    return &currentCompiler->function->chunk;
}

void Parser::emitByte(uint8_t byte) {
    currentChunk()->write(byte, previous.line);
}

void Parser::emitBytes(uint8_t a, uint8_t b) {
    emitByte(a);
    emitByte(b);
}

void Parser::emitReturn() {
    if (currentCompiler->type == FunctionType::INITIALIZER) {
        emitBytes(static_cast<uint8_t>(OpCode::GET_LOCAL), 0);
    } else {
        emitByte(static_cast<uint8_t>(OpCode::NIL));
    }
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
    currentCompiler->scopeDepth++;
}

void Parser::endScope() {
    currentCompiler->scopeDepth--;
    while (currentCompiler->locals.size() > 0 && currentCompiler->locals.back().depth > currentCompiler->scopeDepth) {
        if (currentCompiler->locals.back().isUpvalue) {
            emitByte(static_cast<uint8_t>(OpCode::CLOSE_UPVALUE));
        } else {
            emitByte(static_cast<uint8_t>(OpCode::POP));
        }
        currentCompiler->locals.pop_back();
    }
}

void Parser::defineMethod(ObjString* name) {
    uint8_t constant = makeConstant(Value(name));
    emitBytes(static_cast<uint8_t>(OpCode::METHOD), constant);
}
