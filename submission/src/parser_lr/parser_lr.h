#pragma once
#ifndef PARSER_LR_H
#define PARSER_LR_H

/*
 * LALR(1) Parser — Phase 6 stub.
 * Full implementation to be provided and integrated separately.
 */

#include "../lexer/lexer.h"
#include "../symtable/symbol_table.h"
#include "../common/token.h"
#include "../errorhandler/error_handler.h"

#include <stack>
#include <vector>
#include <string>

struct LRSymbol {
    bool      isTerminal = false;
    TokenType tt         = TokenType::UNKNOWN;
    int       nt         = -1;
    Token     tok;

    static LRSymbol T(TokenType t, const Token& src) {
        LRSymbol s; s.isTerminal=true; s.tt=t; s.tok=src; return s;
    }
    static LRSymbol N(int n) {
        LRSymbol s; s.isTerminal=false; s.nt=n; return s;
    }
};

struct LRProd {
    int         lhsNT  = 0;
    int         rhsLen = 0;
    std::string name;
};

class ParserLR {
public:
    explicit ParserLR(Lexer& lexer, SymbolTable& symTable);

    // Entry point — returns true iff no errors
    bool parse();

    // Print the ACTION table
    void printActionTable() const;

    // Print the GOTO table
    void printGotoTable()   const;

    // Enable / disable step trace
    void setTrace(bool on) { traceMode_ = on; }

private:
    Lexer&       lexer_;
    SymbolTable& sym_;
    bool         hadError_  = false;
    bool         traceMode_ = false;
};

#endif // PARSER_LR_H
