#pragma once
#ifndef PARSER_RD_H
#define PARSER_RD_H

#include "../lexer/lexer.h"
#include "../symtable/symbol_table.h"
#include "../common/token.h"
#include "../errorhandler/error_handler.h"

#include <stdexcept>
#include <string>
#include <vector>

// =============================================================================
// ParseException — thrown on unrecoverable syntax errors.
// =============================================================================
struct ParseException : public std::runtime_error {
    int line, col;
    ParseException(int ln, int cl, const std::string& msg)
        : std::runtime_error(msg), line(ln), col(cl) {}
};

// =============================================================================
// ParserRD — Recursive Descent Parser for the Pascal subset.
//
// Design rules
// ------------
//  • One private method per non-terminal.
//  • match(T)  consumes current token if type == T, throws ParseException
//              (and calls ErrorHandler) on mismatch.
//  • peek()    returns the current look-ahead token without consuming.
//  • advance() unconditionally consumes one token.
//  • Every declaration site calls sym_.insert(...).
//  • Every subprogram enters a new scope for its body (parameters + locals).
//  • Every identifier USE calls sym_.lookup(...); undeclared → error message,
//    no throw (parse continues).
//  • traceMode_: prints "Entering / Exiting parseXxx()" when enabled.
// =============================================================================
class ParserRD {
public:
    ParserRD(Lexer& lexer, SymbolTable& symTable);

    // Entry point. Returns true iff no syntax/semantic errors were encountered.
    bool parse();

    // Enable / disable trace output independently of --verbose.
    void setTrace(bool on) { traceMode_ = on; }

private:
    Lexer&       lexer_;
    SymbolTable& sym_;
    Token        current_;    // one-token look-ahead (LL(1))
    bool         traceMode_;
    bool         hadError_;   // latched true on first error

    // -------------------------------------------------------------------------
    // Collected parameter info — filled by parseParameterList, consumed by
    // parseSubprogramHead after the outer-scope insert is done.
    // -------------------------------------------------------------------------
    struct ParamInfo {
        Token   idTok;
        SymType type;
    };
    std::vector<ParamInfo> pendingParams_;

    // =========================================================================
    // Token management
    // =========================================================================
    const Token& peek() const { return current_; }
    Token advance();
    Token match(TokenType t);
    bool  check(TokenType t) const { return current_.type == t; }

    // =========================================================================
    // Trace helpers
    // =========================================================================
    void traceEnter(const char* name) const;
    void traceExit (const char* name) const;

    // =========================================================================
    // Operator category predicates
    // =========================================================================
    bool isRelop() const;
    bool isAddop() const;
    bool isMulop() const;
    bool isSign()  const;

    // =========================================================================
    // Helper: TokenType → SymType
    // =========================================================================
    static SymType tokenToSymType(TokenType t);

    // =========================================================================
    // Grammar rules — one method per non-terminal
    // =========================================================================

    void               parseProgram();
    std::vector<Token> parseIdentifierList();
    void               parseDeclarations();
    SymType            parseType(bool& isArray, int& arrayLo, int& arrayHi);
    SymType            parseStandardType();
    void               parseSubprogramDeclarations();
    void               parseSubprogramDeclaration();

    // Parses "function/procedure id arguments [: type] ;" and returns the
    // name token.  Does NOT open a new scope — that is done by the caller.
    // Side effect: fills pendingParams_ with parameter info.
    Token   parseSubprogramHead(bool& outIsFunction, SymType& outReturnType);

    // Parses "( parameter_list ) | ε" and appends to pendingParams_.
    // Returns the total parameter count.
    int     parseArguments();

    // Parses "id_list : type { ; id_list : type }" and appends to
    // pendingParams_.  Returns parameter count.
    int     parseParameterList();

    void parseCompoundStatement();
    void parseOptionalStatements();
    void parseStatementList();
    void parseStatement();
    void parseVariable(const Token& idTok);
    void parseProcedureStatement(const Token& idTok);
    void parseExpressionList();
    void parseExpression();
    void parseSimpleExpression();
    void parseTerm();
    void parseFactor();
    void parseSign();
};

#endif // PARSER_RD_H
