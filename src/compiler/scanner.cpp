#include "scanner.hpp"
#include <cstring>
#include <cctype>

Scanner::Scanner(const std::string& source) : source(source) {}

Token Scanner::scanToken() {
    skipWhitespace();
    start = current;

    if (isAtEnd()) return makeToken(TokenType::TOKEN_EOF);

    char c = advance();


    if (isalpha(c)) return identifier();
    if (isdigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '{': return makeToken(TokenType::LEFT_BRACE);
        case '}': return makeToken(TokenType::RIGHT_BRACE);
        case ';': return makeToken(TokenType::SEMICOLON);
        case ',': return makeToken(TokenType::COMMA);
        case '.': return makeToken(TokenType::DOT);
        case '-': return makeToken(TokenType::MINUS);
        case '+': return makeToken(TokenType::PLUS);
        case '/': return makeToken(TokenType::SLASH);
        case '*': return makeToken(TokenType::STAR);
        case '%': return makeToken(TokenType::PERCENT);
        case '?': return makeToken(TokenType::QUESTION);
        case ':': return makeToken(TokenType::COLON);
        case '!': return makeToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
        case '=': return makeToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
        case '<': return makeToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
        case '>': return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

bool Scanner::isAtEnd() const {
    return current >= source.length();
}

char Scanner::advance() {
    current++;
    return source[current - 1];
}

bool Scanner::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    return true;
}

char Scanner::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

char Scanner::peekNext() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

void Scanner::skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Scanner::string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TokenType::STRING);
}

Token Scanner::number() {
    while (isdigit(peek())) advance();

    if (peek() == '.' && isdigit(peekNext())) {
        advance();
        while (isdigit(peek())) advance();
    }

    return makeToken(TokenType::NUMBER);
}

Token Scanner::identifier() {
    while (isalnum(peek())) advance();
    return makeToken(identifierType());
}

TokenType Scanner::identifierType() {
    std::string text = source.substr(start, current - start);
    if (text == "and") return TokenType::AND;
    if (text == "class") return TokenType::CLASS;
    if (text == "else") return TokenType::ELSE;
    if (text == "false") return TokenType::FALSE;
    if (text == "fun") return TokenType::FUN;
    if (text == "for") return TokenType::FOR;
    if (text == "if") return TokenType::IF;
    if (text == "nil") return TokenType::NIL;
    if (text == "or") return TokenType::OR;
    if (text == "print") return TokenType::PRINT;
    if (text == "return") return TokenType::RETURN;
    if (text == "super") return TokenType::SUPER;
    if (text == "this") return TokenType::THIS;
    if (text == "true") return TokenType::TRUE;
    if (text == "var") return TokenType::VAR;
    if (text == "while") return TokenType::WHILE;
    return TokenType::IDENTIFIER;
}

Token Scanner::makeToken(TokenType type) {
    return Token(type, source.c_str() + start, current - start, line);
}

Token Scanner::errorToken(const char* message) {
    return Token(TokenType::ERROR, message, (int)strlen(message), line);
}
