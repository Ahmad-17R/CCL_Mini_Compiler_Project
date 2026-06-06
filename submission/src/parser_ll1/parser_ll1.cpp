#include "parser_ll1.h"
#include "../common/globals.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <stdexcept>

// Convenience aliases
using S  = GrammarSymbol;
using TT = TokenType;

// =============================================================================
// Constructor
// =============================================================================
ParserLL1::ParserLL1(Lexer& lexer, SymbolTable& symTable)
    : lexer_(lexer), sym_(symTable), hadError_(false), traceMode_(false)
{
    buildProductions();
    buildFirstFollow();
    buildTable();
}

// =============================================================================
// parse()
// =============================================================================
bool ParserLL1::parse() {
    if (g_verbose) traceMode_ = true;
    driveParser();
    return !hadError_ && !ErrorHandler::instance().hasErrors();
}

// =============================================================================
// buildProductions — hardcode the complete left-recursion-free grammar
//
// Production numbering mirrors the enum in the header comment block.
// Each production RHS uses S::T(terminal), S::N(nonterm), S::Eps(),
// S::Act(actionId) helpers.
// =============================================================================
void ParserLL1::buildProductions() {
    // Reserve space
    prods_.reserve(60);
    prodNames_.reserve(60);

    // Lambda to add one production
    auto add = [&](std::string name, Production rhs) {
        prods_.push_back(std::move(rhs));
        prodNames_.push_back(std::move(name));
    };

    using N = NT;

    // -------------------------------------------------------------------------
    // P0: program → KW_PROGRAM id LPAREN id_list RPAREN SEMI decls
    //               subprog_decls comp_stmt DOT
    // ACT_SUBPROG_PROC reused to clear collectedIds_ after the header so that
    // the I/O parameter names don't contaminate the first var declaration.
    // -------------------------------------------------------------------------
    add("program → program id ( id_list ) ; decls subprog_decls comp_stmt .",
    { S::T(TT::KW_PROGRAM),
      S::T(TT::ID),           S::Act(ACT_COLLECT_ID),    // collect for program name insert
      S::Act(ACT_INSERT_PROG_NAME),                       // insert PROGRAM entry
      S::T(TT::LPAREN),
      S::N(N::ID_LIST),       // I/O params — consumed but cleared by action below
      S::T(TT::RPAREN),
      S::Act(ACT_SUBPROG_PROC), // clears collectedIds_ / pendingParams_
      S::T(TT::SEMICOLON),
      S::N(N::DECLS),
      S::N(N::SUBPROG_DECLS),
      S::N(N::COMP_STMT),
      S::T(TT::DOT) });

    // -------------------------------------------------------------------------
    // P1: id_list → id id_list_tail
    // -------------------------------------------------------------------------
    add("id_list → id id_list_tail",
    { S::T(TT::ID), S::Act(ACT_COLLECT_ID),
      S::N(N::ID_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P2: id_list_tail → , id id_list_tail
    // -------------------------------------------------------------------------
    add("id_list_tail → , id id_list_tail",
    { S::T(TT::COMMA),
      S::T(TT::ID), S::Act(ACT_COLLECT_ID),
      S::N(N::ID_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P3: id_list_tail → ε
    // -------------------------------------------------------------------------
    add("id_list_tail → ε", {});

    // -------------------------------------------------------------------------
    // P4: decls → var id_list : type ; decls
    // -------------------------------------------------------------------------
    add("decls → var id_list : type ; decls",
    { S::T(TT::KW_VAR),
      S::N(N::ID_LIST),
      S::T(TT::COLON),
      S::N(N::TYPE),
      S::Act(ACT_INSERT_VARS),
      S::T(TT::SEMICOLON),
      S::N(N::DECLS) });

    // -------------------------------------------------------------------------
    // P5: decls → ε
    // -------------------------------------------------------------------------
    add("decls → ε", {});

    // -------------------------------------------------------------------------
    // P6: type → standard_type
    // -------------------------------------------------------------------------
    add("type → standard_type",
    { S::N(N::STD_TYPE) });

    // -------------------------------------------------------------------------
    // P7: type → array [ num .. num ] of standard_type
    // -------------------------------------------------------------------------
    add("type → array [ num .. num ] of standard_type",
    { S::T(TT::KW_ARRAY),   S::Act(ACT_BEGIN_ARRAY_TYPE),
      S::T(TT::LBRACKET),
      S::T(TT::NUM),        S::Act(ACT_SET_ARRAY_START),
      S::T(TT::DOTDOT),
      S::T(TT::NUM),        S::Act(ACT_SET_ARRAY_END),
      S::T(TT::RBRACKET),
      S::T(TT::KW_OF),
      S::N(N::STD_TYPE) });

    // -------------------------------------------------------------------------
    // P8: standard_type → integer
    // -------------------------------------------------------------------------
    add("standard_type → integer",
    { S::T(TT::KW_INTEGER), S::Act(ACT_SET_TYPE_INT) });

    // -------------------------------------------------------------------------
    // P9: standard_type → real
    // -------------------------------------------------------------------------
    add("standard_type → real",
    { S::T(TT::KW_REAL), S::Act(ACT_SET_TYPE_REAL) });

    // -------------------------------------------------------------------------
    // P10: subprog_decls → subprog_decl ; subprog_decls
    // -------------------------------------------------------------------------
    add("subprog_decls → subprog_decl ; subprog_decls",
    { S::N(N::SUBPROG_DECL),
      S::T(TT::SEMICOLON),
      S::N(N::SUBPROG_DECLS) });

    // -------------------------------------------------------------------------
    // P11: subprog_decls → ε
    // -------------------------------------------------------------------------
    add("subprog_decls → ε", {});

    // -------------------------------------------------------------------------
    // P12: subprog_decl → subprog_head decls comp_stmt
    //      ACT_EXIT_SCOPE fires AFTER compound statement (closes body scope)
    // -------------------------------------------------------------------------
    add("subprog_decl → subprog_head decls comp_stmt",
    { S::N(N::SUBPROG_HEAD),
      S::Act(ACT_INSERT_PARAMS),   // flush params into new scope
      S::N(N::DECLS),
      S::N(N::COMP_STMT),
      S::Act(ACT_EXIT_SCOPE) });

    // -------------------------------------------------------------------------
    // P13: subprog_head → function id args : std_type ;
    // -------------------------------------------------------------------------
    add("subprog_head → function id args : std_type ;",
    { S::T(TT::KW_FUNCTION),  S::Act(ACT_SUBPROG_FUNC),
      S::T(TT::ID),            S::Act(ACT_SAVE_SUBPROG_NAME),
      S::N(N::ARGS),
      S::T(TT::COLON),
      S::N(N::STD_TYPE),       S::Act(ACT_SAVE_RETURN_TYPE),
      S::T(TT::SEMICOLON),
      S::Act(ACT_INSERT_SUBPROG),
      S::Act(ACT_ENTER_SCOPE) });

    // -------------------------------------------------------------------------
    // P14: subprog_head → procedure id args ;
    // -------------------------------------------------------------------------
    add("subprog_head → procedure id args ;",
    { S::T(TT::KW_PROCEDURE), S::Act(ACT_SUBPROG_PROC),
      S::T(TT::ID),            S::Act(ACT_SAVE_SUBPROG_NAME),
      S::N(N::ARGS),
      S::T(TT::SEMICOLON),
      S::Act(ACT_INSERT_SUBPROG),
      S::Act(ACT_ENTER_SCOPE) });

    // -------------------------------------------------------------------------
    // P15: args → ( param_list )
    // -------------------------------------------------------------------------
    add("args → ( param_list )",
    { S::T(TT::LPAREN),
      S::N(N::PARAM_LIST),
      S::T(TT::RPAREN) });

    // -------------------------------------------------------------------------
    // P16: args → ε
    // -------------------------------------------------------------------------
    add("args → ε", {});

    // -------------------------------------------------------------------------
    // P17: param_list → id_list : type param_list_tail
    // -------------------------------------------------------------------------
    add("param_list → id_list : type param_list_tail",
    { S::N(N::ID_LIST),
      S::T(TT::COLON),
      S::N(N::TYPE),
      S::Act(ACT_COLLECT_PARAM_GROUP),
      S::N(N::PARAM_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P18: param_list_tail → ; id_list : type param_list_tail
    // -------------------------------------------------------------------------
    add("param_list_tail → ; id_list : type param_list_tail",
    { S::T(TT::SEMICOLON),
      S::N(N::ID_LIST),
      S::T(TT::COLON),
      S::N(N::TYPE),
      S::Act(ACT_COLLECT_PARAM_GROUP),
      S::N(N::PARAM_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P19: param_list_tail → ε
    // -------------------------------------------------------------------------
    add("param_list_tail → ε", {});

    // -------------------------------------------------------------------------
    // P20: comp_stmt → begin opt_stmts end
    // -------------------------------------------------------------------------
    add("comp_stmt → begin opt_stmts end",
    { S::T(TT::KW_BEGIN),
      S::N(N::OPT_STMTS),
      S::T(TT::KW_END) });

    // -------------------------------------------------------------------------
    // P21: opt_stmts → stmt_list
    // -------------------------------------------------------------------------
    add("opt_stmts → stmt_list",
    { S::N(N::STMT_LIST) });

    // -------------------------------------------------------------------------
    // P22: opt_stmts → ε
    // -------------------------------------------------------------------------
    add("opt_stmts → ε", {});

    // -------------------------------------------------------------------------
    // P23: stmt_list → stmt stmt_list_tail
    // -------------------------------------------------------------------------
    add("stmt_list → stmt stmt_list_tail",
    { S::N(N::STMT),
      S::N(N::STMT_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P24: stmt_list_tail → ; stmt stmt_list_tail
    // -------------------------------------------------------------------------
    add("stmt_list_tail → ; stmt stmt_list_tail",
    { S::T(TT::SEMICOLON),
      S::N(N::STMT),
      S::N(N::STMT_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P25: stmt_list_tail → ε
    // -------------------------------------------------------------------------
    add("stmt_list_tail → ε", {});

    // -------------------------------------------------------------------------
    // P26: stmt → id stmt_id_tail
    // -------------------------------------------------------------------------
    add("stmt → id stmt_id_tail",
    { S::T(TT::ID), S::Act(ACT_USE_ID),
      S::N(N::STMT_ID_TAIL) });

    // -------------------------------------------------------------------------
    // P27: stmt → comp_stmt
    // -------------------------------------------------------------------------
    add("stmt → comp_stmt",
    { S::N(N::COMP_STMT) });

    // -------------------------------------------------------------------------
    // P28: stmt → if expr then stmt else stmt
    // -------------------------------------------------------------------------
    add("stmt → if expr then stmt else stmt",
    { S::T(TT::KW_IF),
      S::N(N::EXPR),
      S::T(TT::KW_THEN),
      S::N(N::STMT),
      S::T(TT::KW_ELSE),
      S::N(N::STMT) });

    // -------------------------------------------------------------------------
    // P29: stmt → while expr do stmt
    // -------------------------------------------------------------------------
    add("stmt → while expr do stmt",
    { S::T(TT::KW_WHILE),
      S::N(N::EXPR),
      S::T(TT::KW_DO),
      S::N(N::STMT) });

    // -------------------------------------------------------------------------
    // P30: stmt_id_tail → := expr
    // -------------------------------------------------------------------------
    add("stmt_id_tail → := expr",
    { S::T(TT::ASSIGNOP),
      S::N(N::EXPR) });

    // -------------------------------------------------------------------------
    // P31: stmt_id_tail → [ expr ] := expr
    // -------------------------------------------------------------------------
    add("stmt_id_tail → [ expr ] := expr",
    { S::T(TT::LBRACKET),
      S::N(N::EXPR),
      S::T(TT::RBRACKET),
      S::T(TT::ASSIGNOP),
      S::N(N::EXPR) });

    // -------------------------------------------------------------------------
    // P32: stmt_id_tail → ( expr_list )
    // -------------------------------------------------------------------------
    add("stmt_id_tail → ( expr_list )",
    { S::T(TT::LPAREN),
      S::N(N::EXPR_LIST),
      S::T(TT::RPAREN) });

    // -------------------------------------------------------------------------
    // P33: stmt_id_tail → ε   (bare procedure call)
    // -------------------------------------------------------------------------
    add("stmt_id_tail → ε", {});

    // -------------------------------------------------------------------------
    // P34: expr_list → expr expr_list_tail
    // -------------------------------------------------------------------------
    add("expr_list → expr expr_list_tail",
    { S::N(N::EXPR),
      S::N(N::EXPR_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P35: expr_list_tail → , expr expr_list_tail
    // -------------------------------------------------------------------------
    add("expr_list_tail → , expr expr_list_tail",
    { S::T(TT::COMMA),
      S::N(N::EXPR),
      S::N(N::EXPR_LIST_TAIL) });

    // -------------------------------------------------------------------------
    // P36: expr_list_tail → ε
    // -------------------------------------------------------------------------
    add("expr_list_tail → ε", {});

    // -------------------------------------------------------------------------
    // P37: expr → simple_expr expr_tail
    // -------------------------------------------------------------------------
    add("expr → simple_expr expr_tail",
    { S::N(N::SIMPLE_EXPR),
      S::N(N::EXPR_TAIL) });

    // -------------------------------------------------------------------------
    // P38: expr_tail → relop simple_expr
    // -------------------------------------------------------------------------
    add("expr_tail → = simple_expr",   { S::T(TT::RELOP_EQ),  S::N(N::SIMPLE_EXPR) });
    add("expr_tail → <> simple_expr",  { S::T(TT::RELOP_NEQ), S::N(N::SIMPLE_EXPR) });
    add("expr_tail → < simple_expr",   { S::T(TT::RELOP_LT),  S::N(N::SIMPLE_EXPR) });
    add("expr_tail → <= simple_expr",  { S::T(TT::RELOP_LE),  S::N(N::SIMPLE_EXPR) });
    add("expr_tail → >= simple_expr",  { S::T(TT::RELOP_GE),  S::N(N::SIMPLE_EXPR) });
    add("expr_tail → > simple_expr",   { S::T(TT::RELOP_GT),  S::N(N::SIMPLE_EXPR) });

    // -------------------------------------------------------------------------
    // P44: expr_tail → ε
    // -------------------------------------------------------------------------
    add("expr_tail → ε", {});

    // -------------------------------------------------------------------------
    // P45: simple_expr → sign term simple_expr_tail
    // -------------------------------------------------------------------------
    add("simple_expr → sign term simple_expr_tail",
    { S::N(N::SIGN),
      S::N(N::TERM),
      S::N(N::SIMPLE_EXPR_TAIL) });

    // -------------------------------------------------------------------------
    // P46: simple_expr → term simple_expr_tail
    // -------------------------------------------------------------------------
    add("simple_expr → term simple_expr_tail",
    { S::N(N::TERM),
      S::N(N::SIMPLE_EXPR_TAIL) });

    // -------------------------------------------------------------------------
    // P47: simple_expr_tail → addop term simple_expr_tail
    // -------------------------------------------------------------------------
    add("simple_expr_tail → + term simple_expr_tail",
    { S::T(TT::ADDOP_PLUS),  S::N(N::TERM), S::N(N::SIMPLE_EXPR_TAIL) });
    add("simple_expr_tail → - term simple_expr_tail",
    { S::T(TT::ADDOP_MINUS), S::N(N::TERM), S::N(N::SIMPLE_EXPR_TAIL) });
    add("simple_expr_tail → or term simple_expr_tail",
    { S::T(TT::KW_OR),       S::N(N::TERM), S::N(N::SIMPLE_EXPR_TAIL) });

    // -------------------------------------------------------------------------
    // P51: simple_expr_tail → ε
    // -------------------------------------------------------------------------
    add("simple_expr_tail → ε", {});

    // -------------------------------------------------------------------------
    // P52: term → factor term_tail
    // -------------------------------------------------------------------------
    add("term → factor term_tail",
    { S::N(N::FACTOR),
      S::N(N::TERM_TAIL) });

    // -------------------------------------------------------------------------
    // P53: term_tail → mulop factor term_tail
    // -------------------------------------------------------------------------
    add("term_tail → * factor term_tail",
    { S::T(TT::MULOP_STAR),  S::N(N::FACTOR), S::N(N::TERM_TAIL) });
    add("term_tail → / factor term_tail",
    { S::T(TT::MULOP_SLASH), S::N(N::FACTOR), S::N(N::TERM_TAIL) });
    add("term_tail → div factor term_tail",
    { S::T(TT::KW_DIV),      S::N(N::FACTOR), S::N(N::TERM_TAIL) });
    add("term_tail → mod factor term_tail",
    { S::T(TT::KW_MOD),      S::N(N::FACTOR), S::N(N::TERM_TAIL) });
    add("term_tail → and factor term_tail",
    { S::T(TT::KW_AND),      S::N(N::FACTOR), S::N(N::TERM_TAIL) });

    // -------------------------------------------------------------------------
    // P59: term_tail → ε
    // -------------------------------------------------------------------------
    add("term_tail → ε", {});

    // -------------------------------------------------------------------------
    // P60: factor → id factor_id_tail   (unifies plain var, func call, array)
    // -------------------------------------------------------------------------
    add("factor → id factor_id_tail",
    { S::T(TT::ID),     S::Act(ACT_USE_ID),
      S::N(N::FACTOR_ID_TAIL) });

    // -------------------------------------------------------------------------
    // P61: factor → num
    // -------------------------------------------------------------------------
    add("factor → num",
    { S::T(TT::NUM) });

    // -------------------------------------------------------------------------
    // P62: factor → ( expr )
    // -------------------------------------------------------------------------
    add("factor → ( expr )",
    { S::T(TT::LPAREN),
      S::N(N::EXPR),
      S::T(TT::RPAREN) });

    // -------------------------------------------------------------------------
    // P63: factor → not factor
    // -------------------------------------------------------------------------
    add("factor → not factor",
    { S::T(TT::KW_NOT),
      S::N(N::FACTOR) });

    // -------------------------------------------------------------------------
    // P64: factor_id_tail → ( expr_list )   (function call)
    // -------------------------------------------------------------------------
    add("factor_id_tail → ( expr_list )",
    { S::T(TT::LPAREN),
      S::N(N::EXPR_LIST),
      S::T(TT::RPAREN) });

    // -------------------------------------------------------------------------
    // P65: factor_id_tail → [ expr ]   (array element access)
    // -------------------------------------------------------------------------
    add("factor_id_tail → [ expr ]",
    { S::T(TT::LBRACKET),
      S::N(N::EXPR),
      S::T(TT::RBRACKET) });

    // -------------------------------------------------------------------------
    // P66: factor_id_tail → ε   (plain variable reference)
    // -------------------------------------------------------------------------
    add("factor_id_tail → ε", {});

    // -------------------------------------------------------------------------
    // P66: sign → +
    // -------------------------------------------------------------------------
    add("sign → +", { S::T(TT::ADDOP_PLUS) });

    // -------------------------------------------------------------------------
    // P67: sign → -
    // -------------------------------------------------------------------------
    add("sign → -", { S::T(TT::ADDOP_MINUS) });
}

// =============================================================================
// Production index finder — exact name match
// =============================================================================
static int findProd(const std::vector<std::string>& names,
                    const std::string& name)
{
    for (int i = 0; i < (int)names.size(); ++i)
        if (names[i] == name) return i;
    throw std::logic_error("Production not found: '" + name + "'");
}

// =============================================================================
// buildFirstFollow — hardcoded FIRST and FOLLOW sets
//
// These are hand-derived from the left-recursion-free grammar above.
// Terminals that appear in FIRST sets for nullable non-terminals include ε
// represented as inclusion in the FOLLOW set logic; we store only real tokens.
// =============================================================================
void ParserLL1::buildFirstFollow() {
    using N = NT;

    // Shorthand to add a set of tokens at once
    auto addFirst = [&](NT nt, std::initializer_list<TT> ts) {
        for (auto t : ts) first_[nt].insert(t);
    };
    auto addFollow = [&](NT nt, std::initializer_list<TT> ts) {
        for (auto t : ts) follow_[nt].insert(t);
    };

    // -------------------------------------------------------------------------
    // FIRST sets
    // -------------------------------------------------------------------------
    // FIRST(program)
    addFirst(N::PROGRAM, { TT::KW_PROGRAM });

    // FIRST(id_list) = { id }
    addFirst(N::ID_LIST, { TT::ID });

    // FIRST(id_list_tail) = { COMMA, ε } → store COMMA; ε handled by follow
    addFirst(N::ID_LIST_TAIL, { TT::COMMA });

    // FIRST(decls) = { KW_VAR, ε }
    addFirst(N::DECLS, { TT::KW_VAR });

    // FIRST(type) = FIRST(std_type) ∪ { KW_ARRAY }
    addFirst(N::TYPE, { TT::KW_INTEGER, TT::KW_REAL, TT::KW_ARRAY });

    // FIRST(std_type) = { integer, real }
    addFirst(N::STD_TYPE, { TT::KW_INTEGER, TT::KW_REAL });

    // FIRST(subprog_decls) = { function, procedure, ε }
    addFirst(N::SUBPROG_DECLS, { TT::KW_FUNCTION, TT::KW_PROCEDURE });

    // FIRST(subprog_decl) = { function, procedure }
    addFirst(N::SUBPROG_DECL, { TT::KW_FUNCTION, TT::KW_PROCEDURE });

    // FIRST(subprog_head) = { function, procedure }
    addFirst(N::SUBPROG_HEAD, { TT::KW_FUNCTION, TT::KW_PROCEDURE });

    // FIRST(args) = { LPAREN, ε }
    addFirst(N::ARGS, { TT::LPAREN });

    // FIRST(param_list) = FIRST(id_list) = { id }
    addFirst(N::PARAM_LIST, { TT::ID });

    // FIRST(param_list_tail) = { SEMICOLON, ε }
    addFirst(N::PARAM_LIST_TAIL, { TT::SEMICOLON });

    // FIRST(comp_stmt) = { begin }
    addFirst(N::COMP_STMT, { TT::KW_BEGIN });

    // FIRST(opt_stmts) = FIRST(stmt_list) ∪ {ε}
    //   FIRST(stmt_list) = FIRST(stmt)
    //   FIRST(stmt) = { id, begin, if, while }
    addFirst(N::OPT_STMTS,   { TT::ID, TT::KW_BEGIN, TT::KW_IF, TT::KW_WHILE });

    // FIRST(stmt_list)
    addFirst(N::STMT_LIST,    { TT::ID, TT::KW_BEGIN, TT::KW_IF, TT::KW_WHILE });

    // FIRST(stmt_list_tail) = { SEMICOLON, ε }
    addFirst(N::STMT_LIST_TAIL, { TT::SEMICOLON });

    // FIRST(stmt) = { id, begin, if, while }
    addFirst(N::STMT, { TT::ID, TT::KW_BEGIN, TT::KW_IF, TT::KW_WHILE });

    // FIRST(stmt_id_tail) = { ASSIGNOP, LBRACKET, LPAREN, ε }
    addFirst(N::STMT_ID_TAIL, { TT::ASSIGNOP, TT::LBRACKET, TT::LPAREN });

    // FIRST(expr_list) = FIRST(expr)
    addFirst(N::EXPR_LIST, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT,
                              TT::ADDOP_PLUS, TT::ADDOP_MINUS });

    // FIRST(expr_list_tail) = { COMMA, ε }
    addFirst(N::EXPR_LIST_TAIL, { TT::COMMA });

    // FIRST(expr) = FIRST(simple_expr)
    addFirst(N::EXPR, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT,
                        TT::ADDOP_PLUS, TT::ADDOP_MINUS });

    // FIRST(expr_tail) = { relops, ε }
    addFirst(N::EXPR_TAIL, { TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                              TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT });

    // FIRST(simple_expr) = FIRST(sign) ∪ FIRST(term)
    addFirst(N::SIMPLE_EXPR, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT,
                                TT::ADDOP_PLUS, TT::ADDOP_MINUS });

    // FIRST(simple_expr_tail) = { +, -, or, ε }
    addFirst(N::SIMPLE_EXPR_TAIL, { TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR });

    // FIRST(term) = FIRST(factor)
    addFirst(N::TERM, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT });

    // FIRST(term_tail) = { *, /, div, mod, and, ε }
    addFirst(N::TERM_TAIL, { TT::MULOP_STAR, TT::MULOP_SLASH,
                              TT::KW_DIV, TT::KW_MOD, TT::KW_AND });

    // FIRST(factor) = { id, num, (, not }
    addFirst(N::FACTOR, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT });

    // FIRST(factor_id_tail) = { (, [, ε }
    addFirst(N::FACTOR_ID_TAIL, { TT::LPAREN, TT::LBRACKET });

    // FIRST(sign) = { +, - }
    addFirst(N::SIGN, { TT::ADDOP_PLUS, TT::ADDOP_MINUS });

    // -------------------------------------------------------------------------
    // FOLLOW sets
    // -------------------------------------------------------------------------
    // FOLLOW(program) = { $ }
    addFollow(N::PROGRAM, { TT::EOF_TOKEN });

    // FOLLOW(id_list):
    //   Used in: program(...id_list...): FOLLOW contains RPAREN
    //   Used in: decls var id_list : type: FOLLOW contains COLON
    //   Used in: param_list id_list : type: FOLLOW contains COLON
    addFollow(N::ID_LIST, { TT::RPAREN, TT::COLON });

    // FOLLOW(id_list_tail) = FOLLOW(id_list)
    addFollow(N::ID_LIST_TAIL, { TT::RPAREN, TT::COLON });

    // FOLLOW(decls):
    //   After decls: subprog_decls or comp_stmt starts → function/procedure/begin
    addFollow(N::DECLS, { TT::KW_FUNCTION, TT::KW_PROCEDURE, TT::KW_BEGIN });

    // FOLLOW(type): SEMICOLON follows type in decls and param_list
    addFollow(N::TYPE, { TT::SEMICOLON, TT::RPAREN });

    // FOLLOW(std_type):
    //   In decls: ; follows (via type)
    //   In subprog_head function...: ; follows return type
    //   In param_list: ; or ) follows
    addFollow(N::STD_TYPE, { TT::SEMICOLON, TT::RPAREN });

    // FOLLOW(subprog_decls):
    //   After subprog_decls: comp_stmt → begin
    addFollow(N::SUBPROG_DECLS, { TT::KW_BEGIN });

    // FOLLOW(subprog_decl) = { SEMICOLON }
    addFollow(N::SUBPROG_DECL, { TT::SEMICOLON });

    // FOLLOW(subprog_head):
    //   After subprog_head: decls or comp_stmt → var/begin
    addFollow(N::SUBPROG_HEAD, { TT::KW_VAR, TT::KW_BEGIN });

    // FOLLOW(args):
    //   In function: : follows; in procedure: ; follows
    addFollow(N::ARGS, { TT::COLON, TT::SEMICOLON });

    // FOLLOW(param_list) = { RPAREN }
    addFollow(N::PARAM_LIST, { TT::RPAREN });

    // FOLLOW(param_list_tail) = FOLLOW(param_list) = { RPAREN }
    addFollow(N::PARAM_LIST_TAIL, { TT::RPAREN });

    // FOLLOW(comp_stmt):
    //   At program level: DOT
    //   In subprog_decl: ; (from subprog_decls production)
    //   In stmt: FOLLOW(stmt)
    addFollow(N::COMP_STMT, { TT::DOT, TT::SEMICOLON,
                               TT::KW_END, TT::KW_ELSE });

    // FOLLOW(opt_stmts) = { end }
    addFollow(N::OPT_STMTS, { TT::KW_END });

    // FOLLOW(stmt_list) = FOLLOW(opt_stmts) = { end }
    addFollow(N::STMT_LIST, { TT::KW_END });

    // FOLLOW(stmt_list_tail) = FOLLOW(stmt_list) = { end }
    addFollow(N::STMT_LIST_TAIL, { TT::KW_END });

    // FOLLOW(stmt):
    //   In stmt_list: ; or end
    //   In if..then stmt else stmt: else or follow(stmt)
    //   In while do stmt: follow(stmt)
    addFollow(N::STMT, { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                         TT::DOT });

    // FOLLOW(stmt_id_tail) = FOLLOW(stmt)
    addFollow(N::STMT_ID_TAIL, { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                                  TT::DOT });

    // FOLLOW(expr_list):
    //   In proc call: )
    //   In function factor: )
    addFollow(N::EXPR_LIST, { TT::RPAREN });

    // FOLLOW(expr_list_tail) = FOLLOW(expr_list) = { ) }
    addFollow(N::EXPR_LIST_TAIL, { TT::RPAREN });

    // FOLLOW(expr):
    //   In stmt (assign): ; end else
    //   In if/while: then/do
    //   In expr_list: , )
    //   In array index: ]
    addFollow(N::EXPR, { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                         TT::KW_THEN, TT::KW_DO,
                         TT::COMMA, TT::RPAREN, TT::RBRACKET,
                         TT::DOT });

    // FOLLOW(expr_tail) = FOLLOW(expr)
    addFollow(N::EXPR_TAIL, { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                               TT::KW_THEN, TT::KW_DO,
                               TT::COMMA, TT::RPAREN, TT::RBRACKET,
                               TT::DOT });

    // FOLLOW(simple_expr) = FOLLOW(expr) ∪ relops (from expr_tail)
    addFollow(N::SIMPLE_EXPR, { TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                                 TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                                 TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                                 TT::KW_THEN, TT::KW_DO,
                                 TT::COMMA, TT::RPAREN, TT::RBRACKET,
                                 TT::DOT });

    // FOLLOW(simple_expr_tail) = FOLLOW(simple_expr)
    addFollow(N::SIMPLE_EXPR_TAIL, { TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                                      TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                                      TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                                      TT::KW_THEN, TT::KW_DO,
                                      TT::COMMA, TT::RPAREN, TT::RBRACKET,
                                      TT::DOT });

    // FOLLOW(term) = FOLLOW(simple_expr) ∪ addops
    addFollow(N::TERM, { TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                         TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                         TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                         TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                         TT::KW_THEN, TT::KW_DO,
                         TT::COMMA, TT::RPAREN, TT::RBRACKET,
                         TT::DOT });

    // FOLLOW(term_tail) = FOLLOW(term)
    addFollow(N::TERM_TAIL, { TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                               TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                               TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                               TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                               TT::KW_THEN, TT::KW_DO,
                               TT::COMMA, TT::RPAREN, TT::RBRACKET,
                               TT::DOT });

    // FOLLOW(factor) = FOLLOW(term_tail) ∪ mulops
    addFollow(N::FACTOR, { TT::MULOP_STAR, TT::MULOP_SLASH,
                            TT::KW_DIV, TT::KW_MOD, TT::KW_AND,
                            TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                            TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                            TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                            TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                            TT::KW_THEN, TT::KW_DO,
                            TT::COMMA, TT::RPAREN, TT::RBRACKET,
                            TT::DOT });

    // FOLLOW(factor_id_tail) = FOLLOW(factor)
    addFollow(N::FACTOR_ID_TAIL, { TT::MULOP_STAR, TT::MULOP_SLASH,
                                    TT::KW_DIV, TT::KW_MOD, TT::KW_AND,
                                    TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                                    TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                                    TT::RELOP_LE, TT::RELOP_GE, TT::RELOP_GT,
                                    TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                                    TT::KW_THEN, TT::KW_DO,
                                    TT::COMMA, TT::RPAREN, TT::RBRACKET,
                                    TT::DOT });

    // FOLLOW(sign) = FIRST(term) = { id, num, (, not }
    addFollow(N::SIGN, { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT });
}

// =============================================================================
// buildTable — hardcode the LL(1) parse table
//
// Each entry: table_[NT][lookahead] = production index
// -1 means ERROR (not set means ERROR)
// Entries for nullable non-terminals are set in FOLLOW cells to produce ε
// (an empty production).
// =============================================================================
void ParserLL1::buildTable() {
    using N = NT;

    // Convenience: find a production index by name prefix
    auto P = [&](const std::string& name) -> int {
        return findProd(prodNames_, name);
    };

    // Epsilon productions per non-terminal (for FOLLOW-set cells)
    int p_id_list_tail_eps    = P("id_list_tail → ε");
    int p_decls_eps           = P("decls → ε");
    int p_subprog_decls_eps   = P("subprog_decls → ε");
    int p_args_eps            = P("args → ε");
    int p_param_list_tail_eps = P("param_list_tail → ε");
    int p_opt_stmts_eps       = P("opt_stmts → ε");
    int p_stmt_list_tail_eps  = P("stmt_list_tail → ε");
    int p_stmt_id_tail_eps    = P("stmt_id_tail → ε");
    int p_expr_list_tail_eps  = P("expr_list_tail → ε");
    int p_expr_tail_eps       = P("expr_tail → ε");
    int p_simple_expr_tail_eps= P("simple_expr_tail → ε");
    int p_term_tail_eps       = P("term_tail → ε");
    int p_factor_id_tail_eps  = P("factor_id_tail → ε");

    int p_program             = P("program → program id ( id_list ) ; decls subprog_decls comp_stmt .");
    int p_id_list             = P("id_list → id id_list_tail");
    int p_id_list_tail_comma  = P("id_list_tail → , id id_list_tail");
    int p_decls_var           = P("decls → var id_list : type ; decls");
    int p_type_std            = P("type → standard_type");
    int p_type_array          = P("type → array [ num .. num ] of standard_type");
    int p_std_int             = P("standard_type → integer");
    int p_std_real            = P("standard_type → real");
    int p_subprog_decls_decl  = P("subprog_decls → subprog_decl ; subprog_decls");
    int p_subprog_decl        = P("subprog_decl → subprog_head decls comp_stmt");
    int p_subprog_head_func   = P("subprog_head → function id args : std_type ;");
    int p_subprog_head_proc   = P("subprog_head → procedure id args ;");
    int p_args_lparen         = P("args → ( param_list )");
    int p_param_list          = P("param_list → id_list : type param_list_tail");
    int p_param_list_tail_semi= P("param_list_tail → ; id_list : type param_list_tail");
    int p_comp_stmt           = P("comp_stmt → begin opt_stmts end");
    int p_opt_stmts_list      = P("opt_stmts → stmt_list");
    int p_stmt_list           = P("stmt_list → stmt stmt_list_tail");
    int p_stmt_list_tail_semi = P("stmt_list_tail → ; stmt stmt_list_tail");
    int p_stmt_id             = P("stmt → id stmt_id_tail");
    int p_stmt_comp           = P("stmt → comp_stmt");
    int p_stmt_if             = P("stmt → if expr then stmt else stmt");
    int p_stmt_while          = P("stmt → while expr do stmt");
    int p_stmt_id_tail_assign = P("stmt_id_tail → := expr");
    int p_stmt_id_tail_lbr    = P("stmt_id_tail → [ expr ] := expr");
    int p_stmt_id_tail_lparen = P("stmt_id_tail → ( expr_list )");
    int p_expr_list           = P("expr_list → expr expr_list_tail");
    int p_expr_list_tail_comma= P("expr_list_tail → , expr expr_list_tail");
    int p_expr                = P("expr → simple_expr expr_tail");
    int p_expr_tail_eq        = P("expr_tail → = simple_expr");
    int p_expr_tail_neq       = P("expr_tail → <> simple_expr");
    int p_expr_tail_lt        = P("expr_tail → < simple_expr");
    int p_expr_tail_le        = P("expr_tail → <= simple_expr");
    int p_expr_tail_ge        = P("expr_tail → >= simple_expr");
    int p_expr_tail_gt        = P("expr_tail → > simple_expr");
    int p_simple_expr_sign    = P("simple_expr → sign term simple_expr_tail");
    int p_simple_expr_term    = P("simple_expr → term simple_expr_tail");
    int p_simple_expr_tail_plus  = P("simple_expr_tail → + term simple_expr_tail");
    int p_simple_expr_tail_minus = P("simple_expr_tail → - term simple_expr_tail");
    int p_simple_expr_tail_or    = P("simple_expr_tail → or term simple_expr_tail");
    int p_term                   = P("term → factor term_tail");
    int p_term_tail_star         = P("term_tail → * factor term_tail");
    int p_term_tail_slash        = P("term_tail → / factor term_tail");
    int p_term_tail_div          = P("term_tail → div factor term_tail");
    int p_term_tail_mod          = P("term_tail → mod factor term_tail");
    int p_term_tail_and          = P("term_tail → and factor term_tail");
    int p_factor_id              = P("factor → id factor_id_tail");
    int p_factor_num             = P("factor → num");
    int p_factor_lparen          = P("factor → ( expr )");
    int p_factor_not             = P("factor → not factor");
    int p_factor_id_tail_call    = P("factor_id_tail → ( expr_list )");
    int p_factor_id_tail_arr     = P("factor_id_tail → [ expr ]");
    int p_sign_plus              = P("sign → +");
    int p_sign_minus             = P("sign → -");

    // -------------------------------------------------------------------------
    // PROGRAM
    // -------------------------------------------------------------------------
    table_[N::PROGRAM][TT::KW_PROGRAM] = p_program;

    // -------------------------------------------------------------------------
    // ID_LIST
    // -------------------------------------------------------------------------
    table_[N::ID_LIST][TT::ID] = p_id_list;

    // -------------------------------------------------------------------------
    // ID_LIST_TAIL
    // -------------------------------------------------------------------------
    table_[N::ID_LIST_TAIL][TT::COMMA]     = p_id_list_tail_comma;
    // ε on FOLLOW(id_list_tail) = { ), : }
    table_[N::ID_LIST_TAIL][TT::RPAREN]    = p_id_list_tail_eps;
    table_[N::ID_LIST_TAIL][TT::COLON]     = p_id_list_tail_eps;

    // -------------------------------------------------------------------------
    // DECLS
    // -------------------------------------------------------------------------
    table_[N::DECLS][TT::KW_VAR]           = p_decls_var;
    // ε on FOLLOW(decls)
    table_[N::DECLS][TT::KW_FUNCTION]      = p_decls_eps;
    table_[N::DECLS][TT::KW_PROCEDURE]     = p_decls_eps;
    table_[N::DECLS][TT::KW_BEGIN]         = p_decls_eps;

    // -------------------------------------------------------------------------
    // TYPE
    // -------------------------------------------------------------------------
    table_[N::TYPE][TT::KW_INTEGER]        = p_type_std;
    table_[N::TYPE][TT::KW_REAL]           = p_type_std;
    table_[N::TYPE][TT::KW_ARRAY]          = p_type_array;

    // -------------------------------------------------------------------------
    // STD_TYPE
    // -------------------------------------------------------------------------
    table_[N::STD_TYPE][TT::KW_INTEGER]    = p_std_int;
    table_[N::STD_TYPE][TT::KW_REAL]       = p_std_real;

    // -------------------------------------------------------------------------
    // SUBPROG_DECLS
    // -------------------------------------------------------------------------
    table_[N::SUBPROG_DECLS][TT::KW_FUNCTION]  = p_subprog_decls_decl;
    table_[N::SUBPROG_DECLS][TT::KW_PROCEDURE] = p_subprog_decls_decl;
    // ε on FOLLOW
    table_[N::SUBPROG_DECLS][TT::KW_BEGIN]     = p_subprog_decls_eps;

    // -------------------------------------------------------------------------
    // SUBPROG_DECL
    // -------------------------------------------------------------------------
    table_[N::SUBPROG_DECL][TT::KW_FUNCTION]   = p_subprog_head_func; // delegates to subprog_decl prod
    table_[N::SUBPROG_DECL][TT::KW_PROCEDURE]  = p_subprog_head_proc;
    // The actual subprog_decl production wraps the head, so fix:
    table_[N::SUBPROG_DECL][TT::KW_FUNCTION]   = p_subprog_decl;
    table_[N::SUBPROG_DECL][TT::KW_PROCEDURE]  = p_subprog_decl;

    // -------------------------------------------------------------------------
    // SUBPROG_HEAD
    // -------------------------------------------------------------------------
    table_[N::SUBPROG_HEAD][TT::KW_FUNCTION]   = p_subprog_head_func;
    table_[N::SUBPROG_HEAD][TT::KW_PROCEDURE]  = p_subprog_head_proc;

    // -------------------------------------------------------------------------
    // ARGS
    // -------------------------------------------------------------------------
    table_[N::ARGS][TT::LPAREN]            = p_args_lparen;
    // ε on FOLLOW(args) = { :, ; }
    table_[N::ARGS][TT::COLON]             = p_args_eps;
    table_[N::ARGS][TT::SEMICOLON]         = p_args_eps;

    // -------------------------------------------------------------------------
    // PARAM_LIST
    // -------------------------------------------------------------------------
    table_[N::PARAM_LIST][TT::ID]          = p_param_list;

    // -------------------------------------------------------------------------
    // PARAM_LIST_TAIL
    // -------------------------------------------------------------------------
    table_[N::PARAM_LIST_TAIL][TT::SEMICOLON] = p_param_list_tail_semi;
    // ε on FOLLOW = { ) }
    table_[N::PARAM_LIST_TAIL][TT::RPAREN]    = p_param_list_tail_eps;

    // -------------------------------------------------------------------------
    // COMP_STMT
    // -------------------------------------------------------------------------
    table_[N::COMP_STMT][TT::KW_BEGIN]     = p_comp_stmt;

    // -------------------------------------------------------------------------
    // OPT_STMTS
    // -------------------------------------------------------------------------
    table_[N::OPT_STMTS][TT::ID]           = p_opt_stmts_list;
    table_[N::OPT_STMTS][TT::KW_BEGIN]     = p_opt_stmts_list;
    table_[N::OPT_STMTS][TT::KW_IF]        = p_opt_stmts_list;
    table_[N::OPT_STMTS][TT::KW_WHILE]     = p_opt_stmts_list;
    // ε on FOLLOW = { end }
    table_[N::OPT_STMTS][TT::KW_END]       = p_opt_stmts_eps;

    // -------------------------------------------------------------------------
    // STMT_LIST
    // -------------------------------------------------------------------------
    table_[N::STMT_LIST][TT::ID]            = p_stmt_list;
    table_[N::STMT_LIST][TT::KW_BEGIN]      = p_stmt_list;
    table_[N::STMT_LIST][TT::KW_IF]         = p_stmt_list;
    table_[N::STMT_LIST][TT::KW_WHILE]      = p_stmt_list;

    // -------------------------------------------------------------------------
    // STMT_LIST_TAIL
    // -------------------------------------------------------------------------
    table_[N::STMT_LIST_TAIL][TT::SEMICOLON] = p_stmt_list_tail_semi;
    // ε on FOLLOW = { end }
    table_[N::STMT_LIST_TAIL][TT::KW_END]   = p_stmt_list_tail_eps;

    // -------------------------------------------------------------------------
    // STMT
    // -------------------------------------------------------------------------
    table_[N::STMT][TT::ID]                = p_stmt_id;
    table_[N::STMT][TT::KW_BEGIN]          = p_stmt_comp;
    table_[N::STMT][TT::KW_IF]             = p_stmt_if;
    table_[N::STMT][TT::KW_WHILE]          = p_stmt_while;

    // -------------------------------------------------------------------------
    // STMT_ID_TAIL
    // -------------------------------------------------------------------------
    table_[N::STMT_ID_TAIL][TT::ASSIGNOP]  = p_stmt_id_tail_assign;
    table_[N::STMT_ID_TAIL][TT::LBRACKET]  = p_stmt_id_tail_lbr;
    table_[N::STMT_ID_TAIL][TT::LPAREN]    = p_stmt_id_tail_lparen;
    // ε on FOLLOW(stmt_id_tail) = FOLLOW(stmt)
    for (TT t : { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE, TT::DOT })
        table_[N::STMT_ID_TAIL][t] = p_stmt_id_tail_eps;

    // -------------------------------------------------------------------------
    // EXPR_LIST
    // -------------------------------------------------------------------------
    for (TT t : { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT,
                  TT::ADDOP_PLUS, TT::ADDOP_MINUS })
        table_[N::EXPR_LIST][t] = p_expr_list;

    // -------------------------------------------------------------------------
    // EXPR_LIST_TAIL
    // -------------------------------------------------------------------------
    table_[N::EXPR_LIST_TAIL][TT::COMMA]   = p_expr_list_tail_comma;
    table_[N::EXPR_LIST_TAIL][TT::RPAREN]  = p_expr_list_tail_eps;

    // -------------------------------------------------------------------------
    // EXPR
    // -------------------------------------------------------------------------
    for (TT t : { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT,
                  TT::ADDOP_PLUS, TT::ADDOP_MINUS })
        table_[N::EXPR][t] = p_expr;

    // -------------------------------------------------------------------------
    // EXPR_TAIL
    // -------------------------------------------------------------------------
    table_[N::EXPR_TAIL][TT::RELOP_EQ]     = p_expr_tail_eq;
    table_[N::EXPR_TAIL][TT::RELOP_NEQ]    = p_expr_tail_neq;
    table_[N::EXPR_TAIL][TT::RELOP_LT]     = p_expr_tail_lt;
    table_[N::EXPR_TAIL][TT::RELOP_LE]     = p_expr_tail_le;
    table_[N::EXPR_TAIL][TT::RELOP_GE]     = p_expr_tail_ge;
    table_[N::EXPR_TAIL][TT::RELOP_GT]     = p_expr_tail_gt;
    // ε on FOLLOW(expr_tail) = FOLLOW(expr)
    for (TT t : { TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                  TT::KW_THEN, TT::KW_DO,
                  TT::COMMA, TT::RPAREN, TT::RBRACKET, TT::DOT })
        table_[N::EXPR_TAIL][t] = p_expr_tail_eps;

    // -------------------------------------------------------------------------
    // SIMPLE_EXPR
    // -------------------------------------------------------------------------
    table_[N::SIMPLE_EXPR][TT::ADDOP_PLUS]  = p_simple_expr_sign;
    table_[N::SIMPLE_EXPR][TT::ADDOP_MINUS] = p_simple_expr_sign;
    for (TT t : { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT })
        table_[N::SIMPLE_EXPR][t] = p_simple_expr_term;

    // -------------------------------------------------------------------------
    // SIMPLE_EXPR_TAIL
    // -------------------------------------------------------------------------
    table_[N::SIMPLE_EXPR_TAIL][TT::ADDOP_PLUS]  = p_simple_expr_tail_plus;
    table_[N::SIMPLE_EXPR_TAIL][TT::ADDOP_MINUS] = p_simple_expr_tail_minus;
    table_[N::SIMPLE_EXPR_TAIL][TT::KW_OR]       = p_simple_expr_tail_or;
    // ε on FOLLOW
    for (TT t : { TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                  TT::RELOP_LE, TT::RELOP_GE,  TT::RELOP_GT,
                  TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                  TT::KW_THEN, TT::KW_DO,
                  TT::COMMA, TT::RPAREN, TT::RBRACKET, TT::DOT })
        table_[N::SIMPLE_EXPR_TAIL][t] = p_simple_expr_tail_eps;

    // -------------------------------------------------------------------------
    // TERM
    // -------------------------------------------------------------------------
    for (TT t : { TT::ID, TT::NUM, TT::LPAREN, TT::KW_NOT })
        table_[N::TERM][t] = p_term;

    // -------------------------------------------------------------------------
    // TERM_TAIL
    // -------------------------------------------------------------------------
    table_[N::TERM_TAIL][TT::MULOP_STAR]  = p_term_tail_star;
    table_[N::TERM_TAIL][TT::MULOP_SLASH] = p_term_tail_slash;
    table_[N::TERM_TAIL][TT::KW_DIV]      = p_term_tail_div;
    table_[N::TERM_TAIL][TT::KW_MOD]      = p_term_tail_mod;
    table_[N::TERM_TAIL][TT::KW_AND]      = p_term_tail_and;
    // ε on FOLLOW
    for (TT t : { TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                  TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                  TT::RELOP_LE, TT::RELOP_GE,  TT::RELOP_GT,
                  TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                  TT::KW_THEN, TT::KW_DO,
                  TT::COMMA, TT::RPAREN, TT::RBRACKET, TT::DOT })
        table_[N::TERM_TAIL][t] = p_term_tail_eps;

    // -------------------------------------------------------------------------
    // FACTOR  — uses factor → id factor_id_tail to defer the ( / [ decision
    // -------------------------------------------------------------------------
    table_[N::FACTOR][TT::ID]      = p_factor_id;
    table_[N::FACTOR][TT::NUM]     = p_factor_num;
    table_[N::FACTOR][TT::LPAREN]  = p_factor_lparen;
    table_[N::FACTOR][TT::KW_NOT]  = p_factor_not;

    // -------------------------------------------------------------------------
    // FACTOR_ID_TAIL  — disambiguates after the id has been consumed
    // -------------------------------------------------------------------------
    table_[N::FACTOR_ID_TAIL][TT::LPAREN]   = p_factor_id_tail_call;
    table_[N::FACTOR_ID_TAIL][TT::LBRACKET] = p_factor_id_tail_arr;
    // ε on FOLLOW(factor_id_tail) = FOLLOW(factor)
    for (TT t : { TT::MULOP_STAR, TT::MULOP_SLASH, TT::KW_DIV, TT::KW_MOD, TT::KW_AND,
                  TT::ADDOP_PLUS, TT::ADDOP_MINUS, TT::KW_OR,
                  TT::RELOP_EQ, TT::RELOP_NEQ, TT::RELOP_LT,
                  TT::RELOP_LE, TT::RELOP_GE,  TT::RELOP_GT,
                  TT::SEMICOLON, TT::KW_END, TT::KW_ELSE,
                  TT::KW_THEN, TT::KW_DO,
                  TT::COMMA, TT::RPAREN, TT::RBRACKET, TT::DOT })
        table_[N::FACTOR_ID_TAIL][t] = p_factor_id_tail_eps;

    // -------------------------------------------------------------------------
    // SIGN
    // -------------------------------------------------------------------------
    table_[N::SIGN][TT::ADDOP_PLUS]  = p_sign_plus;
    table_[N::SIGN][TT::ADDOP_MINUS] = p_sign_minus;
}

// =============================================================================
// driveParser — standard LL(1) table-driven loop
// =============================================================================
void ParserLL1::driveParser() {
    std::stack<GrammarSymbol> stk;
    stk.push(S::T(TT::EOF_TOKEN));
    stk.push(S::N(NT::PROGRAM));

    Token lookahead = lexer_.nextToken();
    // Track the last ID and NUM tokens for semantic actions
    Token lastId  (TT::UNKNOWN, "", 0, 0);
    Token lastNum (TT::UNKNOWN, "", 0, 0);

    while (!stk.empty()) {
        GrammarSymbol top = stk.top();

        // ---- epsilon / action symbols are handled without consuming input ----
        if (top.isEpsilon) {
            stk.pop();
            continue;
        }

        if (top.isAction) {
            stk.pop();
            // Pass last-seen ID and NUM into the action
            lastId_  = lastId;
            lastNum_ = lastNum;
            executeAction(top.actionId, lookahead);
            continue;
        }

        if (traceMode_) {
            std::cout << "[LL1] top=" << symbolStr(top)
                      << "  input='" << lookahead.lexeme << "' "
                      << tokenTypeToString(lookahead.type) << "\n";
        }

        if (top.isTerminal) {
            if (top.tt == TT::EOF_TOKEN && lookahead.type == TT::EOF_TOKEN) {
                stk.pop();
                break; // accept
            }
            if (top.tt == lookahead.type) {
                // Before popping: capture ID/NUM for semantic actions
                if (lookahead.type == TT::ID)  lastId  = lookahead;
                if (lookahead.type == TT::NUM) lastNum = lookahead;
                stk.pop();
                lookahead = lexer_.nextToken();
            } else {
                // Terminal mismatch
                std::string msg =
                    "Expected '" + tokenTypeToString(top.tt) +
                    "' but found '" + lookahead.lexeme + "' (" +
                    tokenTypeToString(lookahead.type) + ")";
                ErrorHandler::instance().synError(
                    lookahead.line, lookahead.col, msg);
                hadError_ = true;
                // Skip the offending input token and continue
                lookahead = lexer_.nextToken();
            }
        } else {
            // Non-terminal: consult table
            NT nt = top.nt;
            auto ntIt = table_.find(nt);
            if (ntIt == table_.end()) {
                panicRecover(nt, lookahead);
                stk.pop(); // discard the NT we couldn't expand
                continue;
            }
            auto termIt = ntIt->second.find(lookahead.type);
            if (termIt == ntIt->second.end() || termIt->second < 0) {
                panicRecover(nt, lookahead);
                stk.pop(); // discard the NT we couldn't expand
                continue;
            }
            int prodIdx = termIt->second;
            stk.pop();

            if (traceMode_)
                std::cout << "[LL1]   apply: " << prodNames_[prodIdx] << "\n";

            // Push RHS in reverse order
            const Production& rhs = prods_[prodIdx];
            for (int i = (int)rhs.size() - 1; i >= 0; --i)
                stk.push(rhs[i]);
        }
    }

    if (lookahead.type != TT::EOF_TOKEN) {
        ErrorHandler::instance().synError(
            lookahead.line, lookahead.col,
            "Extra tokens after end of program");
        hadError_ = true;
    }
}

// =============================================================================
// executeAction — fires a semantic action
// =============================================================================
void ParserLL1::executeAction(int id, const Token& /*lookahead*/) {
    switch (id) {

    case ACT_COLLECT_ID:
        // lastId_ was set by driveParser just before firing this action
        collectedIds_.push_back(lastId_);
        break;

    case ACT_SET_TYPE_INT:
        currentType_ = SymType::TYPE_INTEGER;
        break;

    case ACT_SET_TYPE_REAL:
        currentType_ = SymType::TYPE_REAL;
        break;

    case ACT_BEGIN_ARRAY_TYPE:
        inArrayType_ = true;
        break;

    case ACT_SET_ARRAY_START:
        arrayLo_ = std::stoi(lastNum_.lexeme);
        break;

    case ACT_SET_ARRAY_END:
        arrayHi_ = std::stoi(lastNum_.lexeme);
        break;

    case ACT_INSERT_VARS: {
        // Insert all collectedIds_ as variables (or arrays) in current scope
        SymType  kind_type = inArrayType_ ? SymType::TYPE_ARRAY : currentType_;
        SymbolKind kind    = inArrayType_ ? SymbolKind::ARRAY  : SymbolKind::VARIABLE;
        for (const Token& t : collectedIds_) {
            SymbolEntry e;
            e.name        = t.lexeme;
            e.kind        = kind;
            e.type        = kind_type;
            e.line        = t.line;
            e.col         = t.col;
            e.array_start = inArrayType_ ? arrayLo_ : -1;
            e.array_end   = inArrayType_ ? arrayHi_ : -1;
            sym_.insert(e);
        }
        collectedIds_.clear();
        inArrayType_ = false;
        break;
    }

    case ACT_SUBPROG_FUNC:
        inFunction_   = true;
        paramCount_   = 0;
        pendingParams_.clear();
        collectedIds_.clear();
        inArrayType_  = false;
        break;

    case ACT_SUBPROG_PROC:
        inFunction_   = false;
        paramCount_   = 0;
        pendingParams_.clear();
        collectedIds_.clear();
        inArrayType_  = false;
        break;

    case ACT_SAVE_SUBPROG_NAME:
        subprogName_  = lastId_;
        break;

    case ACT_SAVE_RETURN_TYPE:
        subprogRetType_ = currentType_;
        break;

    case ACT_INSERT_SUBPROG: {
        SymbolEntry se;
        se.name        = subprogName_.lexeme;
        se.kind        = inFunction_ ? SymbolKind::FUNCTION : SymbolKind::PROCEDURE;
        se.type        = inFunction_ ? subprogRetType_       : SymType::TYPE_VOID;
        se.line        = subprogName_.line;
        se.col         = subprogName_.col;
        se.param_count = paramCount_;
        sym_.insert(se);
        break;
    }

    case ACT_ENTER_SCOPE:
        sym_.enterScope();
        break;

    case ACT_INSERT_PARAMS:
        // Flush pendingParams_ into the current (just-opened) scope
        for (const ParamGroup& g : pendingParams_) {
            for (const Token& t : g.ids) {
                SymbolEntry pe;
                pe.name = t.lexeme;
                pe.kind = SymbolKind::PARAMETER;
                pe.type = g.type;
                pe.line = t.line;
                pe.col  = t.col;
                sym_.insert(pe);
                ++paramCount_;
            }
        }
        // Backpatch param_count on the already-inserted subprog entry
        if (auto* se = sym_.lookup(subprogName_.lexeme)) {
            // Walk outer scope: the subprog entry is in scope-1
            // sym_.lookup finds innermost, but subprog is in outer scope.
            // We use a direct fix: reinsert is not possible, but we can
            // mutate the entry since lookup returns a pointer.
            // Only update if kind matches (avoids patching a param named same)
            if (se->kind == SymbolKind::FUNCTION ||
                se->kind == SymbolKind::PROCEDURE)
                se->param_count = paramCount_;
        }
        pendingParams_.clear();
        break;

    case ACT_EXIT_SCOPE:
        sym_.exitScope();
        break;

    case ACT_COLLECT_PARAM_GROUP: {
        // collectedIds_ holds the IDs, currentType_ holds the type
        ParamGroup g;
        g.ids  = collectedIds_;
        g.type = inArrayType_ ? SymType::TYPE_ARRAY : currentType_;
        pendingParams_.push_back(std::move(g));
        collectedIds_.clear();
        inArrayType_ = false;
        break;
    }

    case ACT_USE_ID: {
        if (!sym_.lookup(lastId_.lexeme)) {
            ErrorHandler::instance().semError(
                lastId_.line, lastId_.col,
                "Undeclared identifier '" + lastId_.lexeme + "'");
            hadError_ = true;
        }
        break;
    }

    case ACT_INSERT_PROG_NAME: {
        SymbolEntry pe;
        pe.name = lastId_.lexeme;
        pe.kind = SymbolKind::PROGRAM;
        pe.type = SymType::TYPE_VOID;
        pe.line = lastId_.line;
        pe.col  = lastId_.col;
        sym_.insert(pe);
        collectedIds_.clear(); // clear so program name doesn't go into decls
        break;
    }

    default:
        break;
    }
}

// =============================================================================
// =============================================================================
// panicRecover — skip input tokens until one in FOLLOW(nt) is found.
// Always consumes at least one token to guarantee progress.
// =============================================================================
void ParserLL1::panicRecover(NT nt, Token& lookahead) {
    ErrorHandler::instance().synError(
        lookahead.line, lookahead.col,
        std::string("Syntax error at '") + lookahead.lexeme +
        "' - no production for " + ntName(nt));
    hadError_ = true;

    // Find the FOLLOW set for this non-terminal
    const std::set<TT>* follow = nullptr;
    auto it = follow_.find(nt);
    if (it != follow_.end()) follow = &it->second;

    // Always consume the offending token first (guarantees progress),
    // then keep skipping until we land on a FOLLOW token or EOF.
    if (lookahead.type != TT::EOF_TOKEN)
        lookahead = lexer_.nextToken();

    while (lookahead.type != TT::EOF_TOKEN) {
        if (follow && follow->count(lookahead.type)) break;
        lookahead = lexer_.nextToken();
    }
}

// =============================================================================
// printFirstFollowSets
// =============================================================================
void ParserLL1::printFirstFollowSets() const {
    const int W = 22;
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "  FIRST and FOLLOW Sets\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << std::left
              << std::setw(W) << "Non-terminal"
              << std::setw(40) << "FIRST"
              << "FOLLOW\n";
    std::cout << std::string(80, '-') << "\n";

    for (int i = 0; i < (int)NT::_COUNT; ++i) {
        NT nt = static_cast<NT>(i);
        std::string name = ntName(nt);

        // Build FIRST string
        std::string fs;
        auto fit = first_.find(nt);
        if (fit != first_.end())
            for (TT t : fit->second) { if (!fs.empty()) fs += " "; fs += tokenShort(t); }

        // Build FOLLOW string
        std::string fw;
        auto fwit = follow_.find(nt);
        if (fwit != follow_.end())
            for (TT t : fwit->second) { if (!fw.empty()) fw += " "; fw += tokenShort(t); }

        std::cout << std::left
                  << std::setw(W) << name
                  << std::setw(40) << ("{" + fs + "}")
                  << "{" + fw + "}\n";
    }
    std::cout << std::string(80, '=') << "\n\n";
}

// =============================================================================
// printParsingTable
// =============================================================================
void ParserLL1::printParsingTable() const {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "  LL(1) Parsing Table  (NT x terminal -> production index)\n";
    std::cout << std::string(70, '=') << "\n";
    std::cout << std::left << std::setw(22) << "Non-terminal"
              << " | Terminal -> Production\n";
    std::cout << std::string(70, '-') << "\n";

    for (int i = 0; i < (int)NT::_COUNT; ++i) {
        NT nt = static_cast<NT>(i);
        auto it = table_.find(nt);
        if (it == table_.end() || it->second.empty()) continue;

        bool first = true;
        for (const auto& [term, prodIdx] : it->second) {
            if (prodIdx < 0) continue;
            if (first) {
                std::cout << std::left << std::setw(22) << ntName(nt);
                first = false;
            } else {
                std::cout << std::string(22, ' ');
            }
            std::cout << " | " << std::left << std::setw(16) << tokenShort(term)
                      << " -> [" << prodIdx << "] "
                      << prodNames_[prodIdx] << "\n";
        }
    }
    std::cout << std::string(70, '=') << "\n\n";
}

// =============================================================================
// Helpers
// =============================================================================
const char* ParserLL1::ntName(NT nt) {
    switch (nt) {
        case NT::PROGRAM:          return "program";
        case NT::ID_LIST:          return "id_list";
        case NT::ID_LIST_TAIL:     return "id_list_tail";
        case NT::DECLS:            return "decls";
        case NT::TYPE:             return "type";
        case NT::STD_TYPE:         return "std_type";
        case NT::SUBPROG_DECLS:    return "subprog_decls";
        case NT::SUBPROG_DECL:     return "subprog_decl";
        case NT::SUBPROG_HEAD:     return "subprog_head";
        case NT::ARGS:             return "args";
        case NT::PARAM_LIST:       return "param_list";
        case NT::PARAM_LIST_TAIL:  return "param_list_tail";
        case NT::COMP_STMT:        return "comp_stmt";
        case NT::OPT_STMTS:        return "opt_stmts";
        case NT::STMT_LIST:        return "stmt_list";
        case NT::STMT_LIST_TAIL:   return "stmt_list_tail";
        case NT::STMT:             return "stmt";
        case NT::STMT_ID_TAIL:     return "stmt_id_tail";
        case NT::EXPR_LIST:        return "expr_list";
        case NT::EXPR_LIST_TAIL:   return "expr_list_tail";
        case NT::EXPR:             return "expr";
        case NT::EXPR_TAIL:        return "expr_tail";
        case NT::SIMPLE_EXPR:      return "simple_expr";
        case NT::SIMPLE_EXPR_TAIL: return "simple_expr_tail";
        case NT::TERM:             return "term";
        case NT::TERM_TAIL:        return "term_tail";
        case NT::FACTOR:           return "factor";
        case NT::FACTOR_ID_TAIL:   return "factor_id_tail";
        case NT::SIGN:             return "sign";
        default:                   return "?NT?";
    }
}

std::string ParserLL1::symbolStr(const GrammarSymbol& s) const {
    if (s.isEpsilon)   return "ε";
    if (s.isAction)    return "ACT(" + std::to_string(s.actionId) + ")";
    if (s.isTerminal)  return tokenTypeToString(s.tt);
    return ntName(s.nt);
}

std::string ParserLL1::tokenShort(TokenType t) {
    switch (t) {
        case TT::KW_PROGRAM:   return "program";
        case TT::KW_VAR:       return "var";
        case TT::KW_ARRAY:     return "array";
        case TT::KW_OF:        return "of";
        case TT::KW_INTEGER:   return "integer";
        case TT::KW_REAL:      return "real";
        case TT::KW_FUNCTION:  return "function";
        case TT::KW_PROCEDURE: return "procedure";
        case TT::KW_BEGIN:     return "begin";
        case TT::KW_END:       return "end";
        case TT::KW_IF:        return "if";
        case TT::KW_THEN:      return "then";
        case TT::KW_ELSE:      return "else";
        case TT::KW_WHILE:     return "while";
        case TT::KW_DO:        return "do";
        case TT::KW_NOT:       return "not";
        case TT::KW_DIV:       return "div";
        case TT::KW_MOD:       return "mod";
        case TT::KW_AND:       return "and";
        case TT::KW_OR:        return "or";
        case TT::ID:           return "id";
        case TT::NUM:          return "num";
        case TT::ASSIGNOP:     return ":=";
        case TT::RELOP_EQ:     return "=";
        case TT::RELOP_NEQ:    return "<>";
        case TT::RELOP_LT:     return "<";
        case TT::RELOP_LE:     return "<=";
        case TT::RELOP_GE:     return ">=";
        case TT::RELOP_GT:     return ">";
        case TT::ADDOP_PLUS:   return "+";
        case TT::ADDOP_MINUS:  return "-";
        case TT::MULOP_STAR:   return "*";
        case TT::MULOP_SLASH:  return "/";
        case TT::LPAREN:       return "(";
        case TT::RPAREN:       return ")";
        case TT::LBRACKET:     return "[";
        case TT::RBRACKET:     return "]";
        case TT::COMMA:        return ",";
        case TT::SEMICOLON:    return ";";
        case TT::COLON:        return ":";
        case TT::DOT:          return ".";
        case TT::DOTDOT:       return "..";
        case TT::EOF_TOKEN:    return "$";
        default:               return "?";
    }
}
