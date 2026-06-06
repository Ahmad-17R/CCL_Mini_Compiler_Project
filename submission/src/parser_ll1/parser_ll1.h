#pragma once
#ifndef PARSER_LL1_H
#define PARSER_LL1_H

#include "../lexer/lexer.h"
#include "../symtable/symbol_table.h"
#include "../common/token.h"
#include "../errorhandler/error_handler.h"

#include <stack>
#include <vector>
#include <map>
#include <set>
#include <string>

// =============================================================================
// Non-terminal enumeration
// Every left-recursion-free non-terminal gets a unique integer ID.
// The ordering here is the canonical row order of the parsing table.
// =============================================================================
enum class NT : int {
    PROGRAM = 0,
    ID_LIST,
    ID_LIST_TAIL,
    DECLS,
    TYPE,
    STD_TYPE,
    SUBPROG_DECLS,
    SUBPROG_DECL,
    SUBPROG_HEAD,
    ARGS,
    PARAM_LIST,
    PARAM_LIST_TAIL,
    COMP_STMT,
    OPT_STMTS,
    STMT_LIST,
    STMT_LIST_TAIL,
    STMT,
    STMT_ID_TAIL,
    EXPR_LIST,
    EXPR_LIST_TAIL,
    EXPR,
    EXPR_TAIL,
    SIMPLE_EXPR,
    SIMPLE_EXPR_TAIL,
    TERM,
    TERM_TAIL,
    FACTOR,
    FACTOR_ID_TAIL,
    SIGN,
    _COUNT   // sentinel — number of non-terminals
};

// =============================================================================
// GrammarSymbol — one element on the parse stack.
// Can be:
//   • a terminal  (isTerminal == true,  tt holds the TokenType)
//   • a non-terminal (isTerminal == false, nt holds the NT enum)
//   • epsilon marker (isEpsilon == true) — popped immediately, no match
//
// Semantic actions are encoded as special "action symbols":
//   isAction == true, actionId identifies the action.
// =============================================================================
struct GrammarSymbol {
    bool      isTerminal  = false;
    bool      isEpsilon   = false;
    bool      isAction    = false;
    TokenType tt          = TokenType::UNKNOWN;
    NT        nt          = NT::PROGRAM;
    int       actionId    = -1;

    static GrammarSymbol T(TokenType t) {
        GrammarSymbol s; s.isTerminal = true; s.tt = t; return s;
    }
    static GrammarSymbol N(NT n) {
        GrammarSymbol s; s.isTerminal = false; s.nt = n; return s;
    }
    static GrammarSymbol Eps() {
        GrammarSymbol s; s.isEpsilon = true; return s;
    }
    static GrammarSymbol Act(int id) {
        GrammarSymbol s; s.isAction = true; s.actionId = id; return s;
    }
};

// Production = RHS vector of GrammarSymbols (may be empty for ε)
using Production = std::vector<GrammarSymbol>;

// =============================================================================
// ParserLL1 — table-driven predictive (LL(1)) parser
//
// Design
// ------
//  • Productions and the LL(1) table are hardcoded (precomputed).
//  • FIRST / FOLLOW sets are stored for printFirstFollowSets().
//  • Semantic actions are embedded as GrammarSymbol::Act(id) nodes so
//    they fire at the right point during table-driven parsing without
//    breaking the pure LL(1) stack discipline.
//  • Symbol-table integration mirrors the RD parser exactly.
//  • Panic-mode error recovery: on a table error cell, skip input tokens
//    until one in the FOLLOW set of the current non-terminal is found.
// =============================================================================
class ParserLL1 {
public:
    ParserLL1(Lexer& lexer, SymbolTable& symTable);

    // Entry point — returns true iff no errors.
    bool parse();

    // Print FIRST and FOLLOW sets for all non-terminals.
    void printFirstFollowSets() const;

    // Print the complete LL(1) parsing table.
    void printParsingTable() const;

private:
    Lexer&       lexer_;
    SymbolTable& sym_;
    bool         hadError_;
    bool         traceMode_;

    // ---- grammar ------------------------------------------------------------
    std::vector<Production> prods_;      // all productions indexed by prod id
    std::vector<std::string> prodNames_; // human-readable label per production

    // ---- FIRST / FOLLOW -----------------------------------------------------
    std::map<NT, std::set<TokenType>> first_;
    std::map<NT, std::set<TokenType>> follow_;

    // ---- parse table --------------------------------------------------------
    // table_[NT][TokenType] = production index, or -1 (error), or -2 (synch)
    std::map<NT, std::map<TokenType, int>> table_;

    // ---- semantic state for actions -----------------------------------------
    // These are filled by "collect" actions and consumed by "insert" actions.
    std::vector<Token>          collectedIds_;
    bool                        inArrayType_   = false;
    int                         arrayLo_       = 0;
    int                         arrayHi_       = 0;
    SymType                     currentType_   = SymType::TYPE_VOID;
    bool                        inFunction_    = false;
    Token                       subprogName_   = Token(TokenType::UNKNOWN,"",0,0);
    int                         paramCount_    = 0;
    std::vector<ParserLL1*>     pendingScopes_; // unused; scope managed by actions

    // Pending parameters collected before enterScope
    struct ParamGroup { std::vector<Token> ids; SymType type; };
    std::vector<ParamGroup> pendingParams_;
    SymType                 subprogRetType_  = SymType::TYPE_VOID;

    // Last seen ID token (for USE checks)
    Token lastId_  = Token(TokenType::UNKNOWN,"",0,0);
    Token lastNum_ = Token(TokenType::UNKNOWN,"",0,0); // last NUM token

    // ---- initialisation helpers ---------------------------------------------
    void buildProductions();
    void buildFirstFollow();
    void buildTable();

    // ---- driver -------------------------------------------------------------
    void driveParser();

    // ---- semantic actions ---------------------------------------------------
    // Action IDs (must match Act(id) calls in productions)
    enum Action : int {
        // Declaration actions
        ACT_COLLECT_ID         = 1,  // push lastId_ onto collectedIds_
        ACT_SET_TYPE_INT       = 2,  // currentType_ = INTEGER
        ACT_SET_TYPE_REAL      = 3,  // currentType_ = REAL
        ACT_SET_ARRAY_START    = 4,  // arrayLo_ = stoi(lastNum_)
        ACT_SET_ARRAY_END      = 5,  // arrayHi_ = stoi(lastNum_)
        ACT_BEGIN_ARRAY_TYPE   = 6,  // inArrayType_ = true
        ACT_INSERT_VARS        = 7,  // insert collectedIds_ as variables/arrays
        // Subprogram actions
        ACT_SUBPROG_FUNC       = 8,  // inFunction_ = true
        ACT_SUBPROG_PROC       = 9,  // inFunction_ = false
        ACT_SAVE_SUBPROG_NAME  = 10, // subprogName_ = lastId_
        ACT_SAVE_RETURN_TYPE   = 11, // subprogRetType_ = currentType_
        ACT_INSERT_SUBPROG     = 12, // insert func/proc into current scope
        ACT_ENTER_SCOPE        = 13, // sym_.enterScope()
        ACT_INSERT_PARAMS      = 14, // flush pendingParams_ into current scope
        ACT_EXIT_SCOPE         = 15, // sym_.exitScope()
        ACT_COLLECT_PARAM_GROUP= 16, // save collectedIds_+currentType_ as param group
        // USE actions
        ACT_USE_ID             = 17, // sym_.lookup(lastId_), error if missing
        // Program name insert
        ACT_INSERT_PROG_NAME   = 18, // insert lastId_ as PROGRAM entry
    };

    void executeAction(int actionId, const Token& lookahead);

    // ---- error recovery -----------------------------------------------------
    void panicRecover(NT nt, Token& lookahead);

    // ---- helpers ------------------------------------------------------------
    static const char* ntName(NT nt);
    std::string symbolStr(const GrammarSymbol& s) const;
    static std::string tokenShort(TokenType t);
};

#endif // PARSER_LL1_H
