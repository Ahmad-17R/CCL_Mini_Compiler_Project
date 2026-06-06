#include "parser_rd.h"
#include "../common/globals.h"

#include <iostream>
#include <stdexcept>
#include <cctype>

// =============================================================================
// Constructor
// =============================================================================
ParserRD::ParserRD(Lexer& lexer, SymbolTable& symTable)
    : lexer_(lexer), sym_(symTable),
      current_(TokenType::UNKNOWN, "", 0, 0),
      traceMode_(false), hadError_(false)
{}

// =============================================================================
// parse() — public entry point
// =============================================================================
bool ParserRD::parse() {
    // Prime the one-token look-ahead
    current_ = lexer_.nextToken();

    // Honour --verbose flag
    if (g_verbose) traceMode_ = true;

    try {
        parseProgram();
    } catch (const ParseException&) {
        // Already reported to ErrorHandler; just stop parsing
        hadError_ = true;
    } catch (const std::exception& ex) {
        ErrorHandler::instance().synError(0, 0, ex.what());
        hadError_ = true;
    }

    return !hadError_ && !ErrorHandler::instance().hasErrors();
}

// =============================================================================
// Token management
// =============================================================================
Token ParserRD::advance() {
    Token prev = current_;
    current_   = lexer_.nextToken();
    return prev;
}

Token ParserRD::match(TokenType t) {
    if (current_.type != t) {
        std::string msg =
            std::string("Expected '") + tokenTypeToString(t) +
            "' but found '" + current_.lexeme + "' (" +
            tokenTypeToString(current_.type) + ")";
        ErrorHandler::instance().synError(current_.line, current_.col, msg);
        hadError_ = true;
        throw ParseException(current_.line, current_.col, msg);
    }
    return advance();
}

// =============================================================================
// Trace helpers
// =============================================================================
void ParserRD::traceEnter(const char* name) const {
    if (!traceMode_) return;
    std::cout << "[RD]  --> " << name
              << "  ('" << current_.lexeme << "' "
              << tokenTypeToString(current_.type)
              << " @" << current_.line << ":" << current_.col << ")\n";
}

void ParserRD::traceExit(const char* name) const {
    if (!traceMode_) return;
    std::cout << "[RD]  <-- " << name << "\n";
}

// =============================================================================
// Operator predicates
// =============================================================================
bool ParserRD::isRelop() const {
    switch (current_.type) {
        case TokenType::RELOP_EQ:  case TokenType::RELOP_NEQ:
        case TokenType::RELOP_LT:  case TokenType::RELOP_LE:
        case TokenType::RELOP_GE:  case TokenType::RELOP_GT:
            return true;
        default: return false;
    }
}

bool ParserRD::isAddop() const {
    return current_.type == TokenType::ADDOP_PLUS  ||
           current_.type == TokenType::ADDOP_MINUS ||
           current_.type == TokenType::KW_OR;
}

bool ParserRD::isMulop() const {
    return current_.type == TokenType::MULOP_STAR  ||
           current_.type == TokenType::MULOP_SLASH ||
           current_.type == TokenType::KW_DIV      ||
           current_.type == TokenType::KW_MOD      ||
           current_.type == TokenType::KW_AND;
}

bool ParserRD::isSign() const {
    return current_.type == TokenType::ADDOP_PLUS ||
           current_.type == TokenType::ADDOP_MINUS;
}

// =============================================================================
// tokenToSymType
// =============================================================================
SymType ParserRD::tokenToSymType(TokenType t) {
    switch (t) {
        case TokenType::KW_INTEGER: return SymType::TYPE_INTEGER;
        case TokenType::KW_REAL:    return SymType::TYPE_REAL;
        default:                    return SymType::TYPE_VOID;
    }
}

// =============================================================================
// parseProgram
// program → program id ( identifier_list ) ; declarations
//           subprogram_declarations compound_statement .
// =============================================================================
void ParserRD::parseProgram() {
    traceEnter("parseProgram");

    match(TokenType::KW_PROGRAM);
    Token progName = match(TokenType::ID);

    // Insert the program name at global scope (scope 0)
    SymbolEntry pe;
    pe.name = progName.lexeme;
    pe.kind = SymbolKind::PROGRAM;
    pe.type = SymType::TYPE_VOID;
    pe.line = progName.line;
    pe.col  = progName.col;
    sym_.insert(pe);

    match(TokenType::LPAREN);
    // I/O identifier list — consumed but not inserted as variables
    // (these are just the program's file parameters, e.g. input, output)
    parseIdentifierList();
    match(TokenType::RPAREN);
    match(TokenType::SEMICOLON);

    parseDeclarations();
    parseSubprogramDeclarations();
    parseCompoundStatement();

    match(TokenType::DOT);

    traceExit("parseProgram");
}

// =============================================================================
// parseIdentifierList
// identifier_list → id { , id }
// Returns the vector of consumed ID tokens (callers decide what to insert).
// =============================================================================
std::vector<Token> ParserRD::parseIdentifierList() {
    traceEnter("parseIdentifierList");

    std::vector<Token> ids;
    ids.push_back(match(TokenType::ID));
    while (check(TokenType::COMMA)) {
        advance();
        ids.push_back(match(TokenType::ID));
    }

    traceExit("parseIdentifierList");
    return ids;
}

// =============================================================================
// parseDeclarations
// declarations → { var identifier_list : type ; }
// =============================================================================
void ParserRD::parseDeclarations() {
    traceEnter("parseDeclarations");

    while (check(TokenType::KW_VAR)) {
        advance(); // consume 'var'

        std::vector<Token> ids = parseIdentifierList();
        match(TokenType::COLON);

        bool    isArray = false;
        int     lo = -1, hi = -1;
        SymType stype = parseType(isArray, lo, hi);

        match(TokenType::SEMICOLON);

        // Insert every declared name into the current scope
        for (const Token& idTok : ids) {
            SymbolEntry e;
            e.name        = idTok.lexeme;
            e.kind        = isArray ? SymbolKind::ARRAY : SymbolKind::VARIABLE;
            e.type        = isArray ? SymType::TYPE_ARRAY : stype;
            e.line        = idTok.line;
            e.col         = idTok.col;
            e.array_start = lo;
            e.array_end   = hi;
            sym_.insert(e);
        }
    }

    traceExit("parseDeclarations");
}

// =============================================================================
// parseType
// type → standard_type
//      | array [ num .. num ] of standard_type
// =============================================================================
SymType ParserRD::parseType(bool& isArray, int& arrayLo, int& arrayHi) {
    traceEnter("parseType");

    isArray = false;
    arrayLo = -1;
    arrayHi = -1;
    SymType result = SymType::TYPE_VOID;

    if (check(TokenType::KW_ARRAY)) {
        advance(); // consume 'array'
        isArray = true;

        match(TokenType::LBRACKET);
        Token loTok = match(TokenType::NUM);
        match(TokenType::DOTDOT);
        Token hiTok = match(TokenType::NUM);
        match(TokenType::RBRACKET);
        match(TokenType::KW_OF);

        arrayLo = std::stoi(loTok.lexeme);
        arrayHi = std::stoi(hiTok.lexeme);
        result  = parseStandardType(); // element type
    } else {
        result = parseStandardType();
    }

    traceExit("parseType");
    return result;
}

// =============================================================================
// parseStandardType
// standard_type → integer | real
// =============================================================================
SymType ParserRD::parseStandardType() {
    traceEnter("parseStandardType");

    SymType result = SymType::TYPE_VOID;
    if (check(TokenType::KW_INTEGER)) {
        advance();
        result = SymType::TYPE_INTEGER;
    } else if (check(TokenType::KW_REAL)) {
        advance();
        result = SymType::TYPE_REAL;
    } else {
        std::string msg =
            "Expected 'integer' or 'real', got '" + current_.lexeme + "'";
        ErrorHandler::instance().synError(current_.line, current_.col, msg);
        hadError_ = true;
        throw ParseException(current_.line, current_.col, msg);
    }

    traceExit("parseStandardType");
    return result;
}

// =============================================================================
// parseSubprogramDeclarations
// subprogram_declarations → { subprogram_declaration ; }
// =============================================================================
void ParserRD::parseSubprogramDeclarations() {
    traceEnter("parseSubprogramDeclarations");

    while (check(TokenType::KW_FUNCTION) || check(TokenType::KW_PROCEDURE)) {
        parseSubprogramDeclaration();
        match(TokenType::SEMICOLON);
    }

    traceExit("parseSubprogramDeclarations");
}

// =============================================================================
// parseSubprogramDeclaration
// subprogram_declaration → subprogram_head declarations compound_statement
//
// Scope protocol:
//   1. parseSubprogramHead collects parameters into pendingParams_ and
//      inserts the function/procedure into the CURRENT (outer) scope.
//   2. We then enterScope(), flush pendingParams_ as PARAMETER entries,
//      parse local declarations and the body, then exitScope().
// =============================================================================
void ParserRD::parseSubprogramDeclaration() {
    traceEnter("parseSubprogramDeclaration");

    // Step 1: parse header; subprogram inserted into outer scope;
    //         parameter tokens collected in pendingParams_.
    bool    isFunction  = false;
    SymType returnType  = SymType::TYPE_VOID;
    pendingParams_.clear();

    Token nameTok = parseSubprogramHead(isFunction, returnType);
    (void)nameTok;

    // Step 2: open scope for the subprogram body
    sym_.enterScope();

    // Step 3: insert parameters into the new scope
    for (const ParamInfo& pi : pendingParams_) {
        SymbolEntry pe;
        pe.name = pi.idTok.lexeme;
        pe.kind = SymbolKind::PARAMETER;
        pe.type = pi.type;
        pe.line = pi.idTok.line;
        pe.col  = pi.idTok.col;
        sym_.insert(pe);
    }
    pendingParams_.clear();

    // Step 4: local variable declarations
    parseDeclarations();

    // Step 5: body
    parseCompoundStatement();

    // Step 6: close scope
    sym_.exitScope();

    traceExit("parseSubprogramDeclaration");
}

// =============================================================================
// parseSubprogramHead
// subprogram_head → function id arguments : standard_type ;
//                 | procedure id arguments ;
//
// Collects parameters into pendingParams_ (does NOT open/close a scope here).
// Inserts the function/procedure into the current (outer) scope.
// Returns the name Token.
// =============================================================================
Token ParserRD::parseSubprogramHead(bool& outIsFunction, SymType& outReturnType) {
    traceEnter("parseSubprogramHead");

    outIsFunction = check(TokenType::KW_FUNCTION);
    if (outIsFunction)
        advance(); // consume 'function'
    else
        match(TokenType::KW_PROCEDURE);

    Token nameTok = match(TokenType::ID);

    // Parse arguments; parameters appended to pendingParams_
    int paramCount = parseArguments();

    outReturnType = SymType::TYPE_VOID;
    if (outIsFunction) {
        match(TokenType::COLON);
        outReturnType = parseStandardType();
    }
    match(TokenType::SEMICOLON);

    // Insert the subprogram into the current (outer) scope
    SymbolEntry se;
    se.name        = nameTok.lexeme;
    se.kind        = outIsFunction ? SymbolKind::FUNCTION : SymbolKind::PROCEDURE;
    se.type        = outReturnType;
    se.line        = nameTok.line;
    se.col         = nameTok.col;
    se.param_count = paramCount;
    sym_.insert(se);

    traceExit("parseSubprogramHead");
    return nameTok;
}

// =============================================================================
// parseArguments
// arguments → ( parameter_list ) | ε
// Returns the total parameter count; appends to pendingParams_.
// =============================================================================
int ParserRD::parseArguments() {
    traceEnter("parseArguments");

    int count = 0;
    if (check(TokenType::LPAREN)) {
        advance();
        count = parseParameterList();
        match(TokenType::RPAREN);
    }

    traceExit("parseArguments");
    return count;
}

// =============================================================================
// parseParameterList
// parameter_list → identifier_list : type { ; identifier_list : type }
// Appends parameter info to pendingParams_.  Returns total param count.
// =============================================================================
int ParserRD::parseParameterList() {
    traceEnter("parseParameterList");

    int count = 0;

    auto processGroup = [&]() {
        std::vector<Token> ids = parseIdentifierList();
        match(TokenType::COLON);

        bool    isArray = false;
        int     lo = -1, hi = -1;
        SymType stype = parseType(isArray, lo, hi);

        for (const Token& idTok : ids) {
            pendingParams_.push_back({ idTok, isArray ? SymType::TYPE_ARRAY : stype });
            ++count;
        }
    };

    processGroup();
    while (check(TokenType::SEMICOLON)) {
        advance();
        processGroup();
    }

    traceExit("parseParameterList");
    return count;
}

// =============================================================================
// parseCompoundStatement
// compound_statement → begin optional_statements end
// =============================================================================
void ParserRD::parseCompoundStatement() {
    traceEnter("parseCompoundStatement");

    match(TokenType::KW_BEGIN);
    parseOptionalStatements();
    match(TokenType::KW_END);

    traceExit("parseCompoundStatement");
}

// =============================================================================
// parseOptionalStatements
// optional_statements → statement_list | ε
// =============================================================================
void ParserRD::parseOptionalStatements() {
    traceEnter("parseOptionalStatements");

    // ε when next token is 'end'
    if (!check(TokenType::KW_END))
        parseStatementList();

    traceExit("parseOptionalStatements");
}

// =============================================================================
// parseStatementList
// statement_list → statement { ; statement }
//
// Left-recursion-free iterative version.
// A trailing semicolon before 'end' is tolerated (common Pascal style).
// =============================================================================
void ParserRD::parseStatementList() {
    traceEnter("parseStatementList");

    parseStatement();
    while (check(TokenType::SEMICOLON)) {
        advance();
        // A semicolon just before 'end' is a trailing semicolon — skip it.
        if (check(TokenType::KW_END)) break;
        parseStatement();
    }

    traceExit("parseStatementList");
}

// =============================================================================
// parseStatement
// statement → variable := expression
//           | procedure_statement
//           | compound_statement
//           | if expression then statement else statement
//           | while expression do statement
//
// Disambiguation: both "variable := expr" and "procedure_statement" begin
// with an ID.  We consume the ID and then inspect the next token:
//   - '['  or ':='  → variable assignment (array index OR simple assignment)
//   - '('  or anything else → procedure statement
// =============================================================================
void ParserRD::parseStatement() {
    traceEnter("parseStatement");

    if (check(TokenType::KW_BEGIN)) {
        parseCompoundStatement();

    } else if (check(TokenType::KW_IF)) {
        advance();
        parseExpression();
        match(TokenType::KW_THEN);
        parseStatement();
        // 'else' is mandatory in this grammar (no optional else)
        match(TokenType::KW_ELSE);
        parseStatement();

    } else if (check(TokenType::KW_WHILE)) {
        advance();
        parseExpression();
        match(TokenType::KW_DO);
        parseStatement();

    } else if (check(TokenType::ID)) {
        // Consume the ID first, then decide
        Token idTok = advance();

        // Verify the identifier is declared
        if (!sym_.lookup(idTok.lexeme)) {
            ErrorHandler::instance().semError(
                idTok.line, idTok.col,
                "Undeclared identifier '" + idTok.lexeme + "'");
            hadError_ = true;
        }

        if (check(TokenType::ASSIGNOP) || check(TokenType::LBRACKET)) {
            // variable := expression
            parseVariable(idTok);
            match(TokenType::ASSIGNOP);

            // Type-mismatch check: if lhs is integer and rhs is a real literal
            const SymbolEntry* lhsEntry = sym_.lookup(idTok.lexeme);
            if (lhsEntry && lhsEntry->type == SymType::TYPE_INTEGER
                         && peek().type == TokenType::NUM
                         && peek().lexeme.find('.') != std::string::npos) {
                ErrorHandler::instance().semError(
                    peek().line, peek().col,
                    "Type mismatch: cannot assign real value to integer variable '"
                    + idTok.lexeme + "'");
            }

            parseExpression();
        } else {
            // procedure call: id [ ( expression_list ) ]
            const SymbolEntry* callee = sym_.lookup(idTok.lexeme);
            if (callee && callee->kind == SymbolKind::VARIABLE) {
                ErrorHandler::instance().semError(
                    idTok.line, idTok.col,
                    "'" + idTok.lexeme + "' is a variable, not a procedure");
            }
            parseProcedureStatement(idTok);
        }

    } else {
        // Empty statement (e.g. two consecutive semicolons) is silently ignored,
        // but an unexpected token that can't start a statement is an error.
        if (!check(TokenType::KW_END) &&
            !check(TokenType::EOF_TOKEN)) {
            std::string msg =
                "Unexpected token '" + current_.lexeme + "' in statement";
            ErrorHandler::instance().synError(current_.line, current_.col, msg);
            hadError_ = true;
            throw ParseException(current_.line, current_.col, msg);
        }
    }

    traceExit("parseStatement");
}

// =============================================================================
// parseVariable
// variable → id [ '[' expression ']' ]
// idTok: the ID that was already consumed by parseStatement().
// =============================================================================
void ParserRD::parseVariable(const Token& idTok) {
    traceEnter("parseVariable");
    (void)idTok; // ID already consumed and checked by parseStatement

    if (check(TokenType::LBRACKET)) {
        advance();
        parseExpression();
        match(TokenType::RBRACKET);
    }

    traceExit("parseVariable");
}

// =============================================================================
// parseProcedureStatement
// procedure_statement → id [ ( expression_list ) ]
// idTok: already consumed.
// =============================================================================
void ParserRD::parseProcedureStatement(const Token& idTok) {
    traceEnter("parseProcedureStatement");
    (void)idTok;

    if (check(TokenType::LPAREN)) {
        advance();
        parseExpressionList();
        match(TokenType::RPAREN);
    }
    // bare call with no arguments is also valid

    traceExit("parseProcedureStatement");
}

// =============================================================================
// parseExpressionList
// expression_list → expression { , expression }
// =============================================================================
void ParserRD::parseExpressionList() {
    traceEnter("parseExpressionList");

    parseExpression();
    while (check(TokenType::COMMA)) {
        advance();
        parseExpression();
    }

    traceExit("parseExpressionList");
}

// =============================================================================
// parseExpression
// expression → simple_expression [ relop simple_expression ]
// =============================================================================
void ParserRD::parseExpression() {
    traceEnter("parseExpression");

    parseSimpleExpression();
    if (isRelop()) {
        advance(); // consume the relop
        parseSimpleExpression();
    }

    traceExit("parseExpression");
}

// =============================================================================
// parseSimpleExpression
// simple_expression → [ sign ] term { addop term }
//
// Left-recursion-free iterative version.
// =============================================================================
void ParserRD::parseSimpleExpression() {
    traceEnter("parseSimpleExpression");

    if (isSign())
        parseSign();

    parseTerm();
    while (isAddop()) {
        advance(); // consume addop
        parseTerm();
    }

    traceExit("parseSimpleExpression");
}

// =============================================================================
// parseTerm
// term → factor { mulop factor }
//
// Left-recursion-free iterative version.
// =============================================================================
void ParserRD::parseTerm() {
    traceEnter("parseTerm");

    parseFactor();
    while (isMulop()) {
        advance(); // consume mulop
        parseFactor();
    }

    traceExit("parseTerm");
}

// =============================================================================
// parseFactor
// factor → id [ ( expression_list ) ]
//        | num
//        | ( expression )
//        | not factor
// =============================================================================
void ParserRD::parseFactor() {
    traceEnter("parseFactor");

    if (check(TokenType::NUM)) {
        advance();

    } else if (check(TokenType::LPAREN)) {
        advance();
        parseExpression();
        match(TokenType::RPAREN);

    } else if (check(TokenType::KW_NOT)) {
        advance();
        parseFactor(); // right-recursive (grammar allows it, no infinite loop)

    } else if (check(TokenType::ID)) {
        Token idTok = advance();

        // Symbol-table USE check
        if (!sym_.lookup(idTok.lexeme)) {
            ErrorHandler::instance().semError(
                idTok.line, idTok.col,
                "Undeclared identifier '" + idTok.lexeme + "'");
            hadError_ = true;
        }

        if (check(TokenType::LPAREN)) {
            // Function call: id ( expression_list )
            advance();
            parseExpressionList();
            match(TokenType::RPAREN);
        } else if (check(TokenType::LBRACKET)) {
            // Array element access: id [ expression ]
            advance();
            parseExpression();
            match(TokenType::RBRACKET);
        }
        // else: plain variable reference

    } else {
        std::string msg =
            "Unexpected token '" + current_.lexeme +
            "' (" + tokenTypeToString(current_.type) + ") in factor";
        ErrorHandler::instance().synError(current_.line, current_.col, msg);
        hadError_ = true;
        throw ParseException(current_.line, current_.col, msg);
    }

    traceExit("parseFactor");
}

// =============================================================================
// parseSign
// sign → + | -
// =============================================================================
void ParserRD::parseSign() {
    traceEnter("parseSign");

    if (check(TokenType::ADDOP_PLUS) || check(TokenType::ADDOP_MINUS))
        advance();
    else {
        std::string msg =
            "Expected sign ('+' or '-'), got '" + current_.lexeme + "'";
        ErrorHandler::instance().synError(current_.line, current_.col, msg);
        hadError_ = true;
        throw ParseException(current_.line, current_.col, msg);
    }

    traceExit("parseSign");
}
