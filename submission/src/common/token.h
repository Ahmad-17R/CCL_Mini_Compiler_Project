#pragma once
#ifndef TOKEN_H
#define TOKEN_H

#include <string>

// =============================================================================
// TokenType Enum — all terminal symbols for the Pascal subset grammar
// =============================================================================
enum class TokenType {
    // -------------------------------------------------------------------------
    // Keywords
    // -------------------------------------------------------------------------
    KW_PROGRAM,       // program
    KW_VAR,           // var
    KW_ARRAY,         // array
    KW_OF,            // of
    KW_INTEGER,       // integer
    KW_REAL,          // real
    KW_FUNCTION,      // function
    KW_PROCEDURE,     // procedure
    KW_BEGIN,         // begin
    KW_END,           // end
    KW_IF,            // if
    KW_THEN,          // then
    KW_ELSE,          // else
    KW_WHILE,         // while
    KW_DO,            // do
    KW_NOT,           // not
    KW_DIV,           // div  (integer division)
    KW_MOD,           // mod
    KW_AND,           // and
    KW_OR,            // or

    // -------------------------------------------------------------------------
    // Identifiers & Literals
    // -------------------------------------------------------------------------
    ID,               // letter (letter | digit)*
    NUM,              // integer or real numeric literal

    // -------------------------------------------------------------------------
    // Operators — assignop
    // -------------------------------------------------------------------------
    ASSIGNOP,         // :=

    // -------------------------------------------------------------------------
    // Operators — relop
    // -------------------------------------------------------------------------
    RELOP_EQ,         // =
    RELOP_NEQ,        // <>
    RELOP_LT,         // <
    RELOP_LE,         // <=
    RELOP_GE,         // >=
    RELOP_GT,         // >

    // -------------------------------------------------------------------------
    // Operators — addop
    // -------------------------------------------------------------------------
    ADDOP_PLUS,       // +
    ADDOP_MINUS,      // -
    // ADDOP_OR is covered by KW_OR

    // -------------------------------------------------------------------------
    // Operators — mulop
    // -------------------------------------------------------------------------
    MULOP_STAR,       // *
    MULOP_SLASH,      // /
    // MULOP_DIV is covered by KW_DIV
    // MULOP_MOD is covered by KW_MOD
    // MULOP_AND is covered by KW_AND

    // -------------------------------------------------------------------------
    // Punctuation / Delimiters
    // -------------------------------------------------------------------------
    LPAREN,           // (
    RPAREN,           // )
    LBRACKET,         // [
    RBRACKET,         // ]
    COMMA,            // ,
    SEMICOLON,        // ;
    COLON,            // :
    DOT,              // .
    DOTDOT,           // ..

    // -------------------------------------------------------------------------
    // Special
    // -------------------------------------------------------------------------
    EOF_TOKEN,        // end of input
    UNKNOWN           // unrecognised character
};

// =============================================================================
// Token Struct
// =============================================================================
struct Token {
    TokenType   type;
    std::string lexeme;
    int         line;
    int         col;

    Token()
        : type(TokenType::UNKNOWN), lexeme(""), line(0), col(0) {}

    Token(TokenType t, const std::string& lex, int ln, int cl)
        : type(t), lexeme(lex), line(ln), col(cl) {}
};

// =============================================================================
// Helper: convert TokenType to a readable string (useful for debug/error msgs)
// =============================================================================
inline std::string tokenTypeToString(TokenType t) {
    switch (t) {
        // Keywords
        case TokenType::KW_PROGRAM:   return "KW_PROGRAM";
        case TokenType::KW_VAR:       return "KW_VAR";
        case TokenType::KW_ARRAY:     return "KW_ARRAY";
        case TokenType::KW_OF:        return "KW_OF";
        case TokenType::KW_INTEGER:   return "KW_INTEGER";
        case TokenType::KW_REAL:      return "KW_REAL";
        case TokenType::KW_FUNCTION:  return "KW_FUNCTION";
        case TokenType::KW_PROCEDURE: return "KW_PROCEDURE";
        case TokenType::KW_BEGIN:     return "KW_BEGIN";
        case TokenType::KW_END:       return "KW_END";
        case TokenType::KW_IF:        return "KW_IF";
        case TokenType::KW_THEN:      return "KW_THEN";
        case TokenType::KW_ELSE:      return "KW_ELSE";
        case TokenType::KW_WHILE:     return "KW_WHILE";
        case TokenType::KW_DO:        return "KW_DO";
        case TokenType::KW_NOT:       return "KW_NOT";
        case TokenType::KW_DIV:       return "KW_DIV";
        case TokenType::KW_MOD:       return "KW_MOD";
        case TokenType::KW_AND:       return "KW_AND";
        case TokenType::KW_OR:        return "KW_OR";
        // Identifiers & literals
        case TokenType::ID:           return "ID";
        case TokenType::NUM:          return "NUM";
        // Assignop
        case TokenType::ASSIGNOP:     return "ASSIGNOP(:=)";
        // Relop
        case TokenType::RELOP_EQ:     return "RELOP(=)";
        case TokenType::RELOP_NEQ:    return "RELOP(<>)";
        case TokenType::RELOP_LT:     return "RELOP(<)";
        case TokenType::RELOP_LE:     return "RELOP(<=)";
        case TokenType::RELOP_GE:     return "RELOP(>=)";
        case TokenType::RELOP_GT:     return "RELOP(>)";
        // Addop
        case TokenType::ADDOP_PLUS:   return "ADDOP(+)";
        case TokenType::ADDOP_MINUS:  return "ADDOP(-)";
        // Mulop
        case TokenType::MULOP_STAR:   return "MULOP(*)";
        case TokenType::MULOP_SLASH:  return "MULOP(/)";
        // Punctuation
        case TokenType::LPAREN:       return "LPAREN";
        case TokenType::RPAREN:       return "RPAREN";
        case TokenType::LBRACKET:     return "LBRACKET";
        case TokenType::RBRACKET:     return "RBRACKET";
        case TokenType::COMMA:        return "COMMA";
        case TokenType::SEMICOLON:    return "SEMICOLON";
        case TokenType::COLON:        return "COLON";
        case TokenType::DOT:          return "DOT";
        case TokenType::DOTDOT:       return "DOTDOT";
        // Special
        case TokenType::EOF_TOKEN:    return "EOF";
        case TokenType::UNKNOWN:      return "UNKNOWN";
        default:                      return "???";
    }
}

#endif // TOKEN_H
