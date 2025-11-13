#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class TokenType {
    LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
    COMMA, DOT, MINUS, PLUS, SEMICOLON, SLASH, STAR,
    BANG, BANG_EQUAL, EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL, LESS, LESS_EQUAL,
    IDENTIFIER, STRING, NUMBER,
    AND, CLASS, ELSE, FALSE, FUN, FOR, IF, NIL, OR,
    PRINT, RETURN, SUPER, THIS, TRUE, VAR, WHILE,
    ERROR, EOF
};

struct Token {
    TokenType type;
    const char* start;
    int length;
    int line;
    std::string toString() const;
};

class Scanner {
public:
    Scanner(const std::string& source);
    std::vector<Token> scanTokens();

private:
    void scanToken();
    void addToken(TokenType type);
    void addToken(TokenType type, std::string literal);
    bool isAtEnd() const;
    char advance();
    bool match(char expected);
    char peek() const;
    char peekNext() const;
    void string();
    void number();
    void identifier();

    const std::string source;
    std::vector<Token> tokens;
    int start = 0, current = 0, line = 1;
    std::unordered_map<std::string, TokenType> keywords;
};
