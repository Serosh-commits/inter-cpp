#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class TokenType {
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
    LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, DOT, MINUS, PLUS, SEMICOLON, SLASH, STAR, PERCENT,
    AMPERSAND, PIPE, CARET, TILDE, LESS_LESS, GREATER_GREATER,
    BANG, BANG_EQUAL, EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL, LESS, LESS_EQUAL,
    IDENTIFIER, STRING, NUMBER,
    AND, CLASS, ELSE, FALSE, FUN, FOR, IF, NIL, OR,
    PRINT, RETURN, SUPER, THIS, TRUE, VAR, WHILE,
    ERROR, TOKEN_EOF,
    QUESTION, COLON
};

struct Token {
    TokenType type;
    const char* start;
    int length;
    int line;
    Token() = default;
    Token(TokenType type, const char* start, int length, int line)
        : type(type), start(start), length(length), line(line) {}
    std::string toString() const;
};

class Scanner {
public:
    Scanner(const std::string& source);
    Token scanToken();
    bool isAtEnd() const;

private:
    void skipWhitespace();
    TokenType checkKeyword(int start, int length, const char* rest, TokenType type);
    TokenType identifierType();
    Token makeToken(TokenType type);
    Token errorToken(const char* message);
    char advance();
    bool match(char expected);
    char peek() const;
    char peekNext() const;
    Token string();
    Token number();
    Token identifier();

    const std::string source;
    int start = 0, current = 0, line = 1;
};
