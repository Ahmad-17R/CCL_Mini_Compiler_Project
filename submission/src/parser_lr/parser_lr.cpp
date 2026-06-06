/*
 * parser_lr.cpp  —  SLR(1) Shift-Reduce Parser   (Phase 6)
 * CS-471L Compiler Construction Lab, UET Lahore, Spring 2026
 *
 * ==========================================================================
 * Architecture  (follows Sample Project 3 / Aho-Ullman "Dragon Book")
 * ==========================================================================
 *
 * Step 1 – Grammar encoding
 *   The Pascal-subset grammar is stored as a vector<Production>.  Each
 *   production carries its LHS non-terminal, an ordered list of grammar
 *   symbols (terminals or non-terminals), and a human-readable string.
 *
 * Step 2 – FIRST / nullable / FOLLOW
 *   Standard fixed-point algorithms over the production list.
 *   FOLLOW is needed to decide when to reduce (SLR(1) condition).
 *
 * Step 3 – Canonical LR(0) item-set collection
 *   closure() and doGoto() build every item set.  Each set becomes one
 *   parser state.  Transitions are recorded in shiftGoto_ (terminals)
 *   and gotoTab_ (non-terminals).
 *
 * Step 4 – ACTION table (SLR(1))
 *   • For every item [A → α • a β] with a terminal a: ACTION[i][a] = shift j
 *   • For every item [A → α •] and every a ∈ FOLLOW(A): ACTION[i][a] = reduce
 *   • Shift/reduce conflicts are resolved by preferring shift (handles
 *     the dangling-else).
 *
 * Step 5 – Parse loop
 *   Two explicit stacks:
 *     stateStack   — vector<int>
 *     symbolStack  — vector<GSymbol>   (carries Token for terminals)
 *   Standard while-loop: peek ACTION[top][lookahead] → shift / reduce / accept.
 *
 * Step 6 – Trace
 *   When verbose mode is on, every step prints:
 *     [state stack]  |  [symbol stack]  |  [remaining input]  |  action
 * ==========================================================================
 */

#include "parser_lr.h"
#include "../common/globals.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
//  Non-terminal identifiers
// ============================================================================
enum class NT : int {
    S_PRIME = 0,       // augmented start  S' → program
    PROGRAM,
    IDENTIFIER_LIST,
    DECLARATIONS,
    TYPE,
    STANDARD_TYPE,
    SUBPROGRAM_DECLARATIONS,
    SUBPROGRAM_DECLARATION,
    SUBPROGRAM_HEAD,
    ARGUMENTS,
    PARAMETER_LIST,
    COMPOUND_STATEMENT,
    OPTIONAL_STATEMENTS,
    STATEMENT_LIST,
    STATEMENT,
    VARIABLE,
    PROCEDURE_STATEMENT,
    EXPRESSION_LIST,
    EXPRESSION,
    SIMPLE_EXPRESSION,
    TERM,
    FACTOR,
    SIGN,
    _COUNT
};
static constexpr int NT_COUNT = static_cast<int>(NT::_COUNT);

static std::string ntStr(NT n) {
    switch (n) {
        case NT::S_PRIME:                 return "S'";
        case NT::PROGRAM:                 return "program";
        case NT::IDENTIFIER_LIST:         return "identifier_list";
        case NT::DECLARATIONS:            return "declarations";
        case NT::TYPE:                    return "type";
        case NT::STANDARD_TYPE:           return "standard_type";
        case NT::SUBPROGRAM_DECLARATIONS: return "subprogram_declarations";
        case NT::SUBPROGRAM_DECLARATION:  return "subprogram_declaration";
        case NT::SUBPROGRAM_HEAD:         return "subprogram_head";
        case NT::ARGUMENTS:               return "arguments";
        case NT::PARAMETER_LIST:          return "parameter_list";
        case NT::COMPOUND_STATEMENT:      return "compound_statement";
        case NT::OPTIONAL_STATEMENTS:     return "optional_statements";
        case NT::STATEMENT_LIST:          return "statement_list";
        case NT::STATEMENT:               return "statement";
        case NT::VARIABLE:                return "variable";
        case NT::PROCEDURE_STATEMENT:     return "procedure_statement";
        case NT::EXPRESSION_LIST:         return "expression_list";
        case NT::EXPRESSION:              return "expression";
        case NT::SIMPLE_EXPRESSION:       return "simple_expression";
        case NT::TERM:                    return "term";
        case NT::FACTOR:                  return "factor";
        case NT::SIGN:                    return "sign";
        default:                          return "?NT";
    }
}

// ============================================================================
//  Grammar symbol  (terminal or non-terminal)
// ============================================================================
struct GSymbol {
    enum Kind { TERM, NONTERM } kind;
    TokenType tt;   // valid when TERM
    NT        nt;   // valid when NONTERM
    Token     tok;  // carries lexeme/line/col for terminals on the symbol stack

    static GSymbol T(TokenType t) {
        GSymbol s; s.kind = TERM; s.tt = t;
        s.nt = NT::_COUNT; return s;
    }
    static GSymbol N(NT n) {
        GSymbol s; s.kind = NONTERM; s.nt = n;
        s.tt = TokenType::UNKNOWN; return s;
    }
    bool operator==(const GSymbol& o) const {
        if (kind != o.kind) return false;
        return kind == TERM ? tt == o.tt : nt == o.nt;
    }
    bool operator<(const GSymbol& o) const {
        if (kind != o.kind) return kind < o.kind;
        return kind == TERM
            ? static_cast<int>(tt) < static_cast<int>(o.tt)
            : static_cast<int>(nt) < static_cast<int>(o.nt);
    }
    std::string str() const {
        if (kind == TERM) return tokenTypeToString(tt);
        return ntStr(nt);
    }
};

// ============================================================================
//  Production
// ============================================================================
struct Production {
    NT                   lhs;
    std::vector<GSymbol> rhs;
    std::string          text;
};

// ============================================================================
//  LR(0) item
// ============================================================================
struct Item {
    int prodIdx;
    int dot;
    bool operator==(const Item& o) const {
        return prodIdx == o.prodIdx && dot == o.dot;
    }
    bool operator<(const Item& o) const {
        if (prodIdx != o.prodIdx) return prodIdx < o.prodIdx;
        return dot < o.dot;
    }
};
using ItemSet = std::set<Item>;

// ============================================================================
//  Action entry
// ============================================================================
enum class AKind { SHIFT, REDUCE, ACCEPT };
struct Action {
    AKind kind;
    int   value; // state# for SHIFT, prodIdx for REDUCE
};

// ============================================================================
//  All parser internals — kept in a struct so parse() is self-contained
// ============================================================================
struct LRTables {
    std::vector<Production>   prods;
    std::vector<ItemSet>      states;
    std::map<std::pair<int,TokenType>, int>    shiftGoto;  // state×terminal→state
    std::map<std::pair<int,NT>,        int>    gotoTab;    // state×NT→state
    std::map<std::pair<int,TokenType>, Action> actionTab;
    std::vector<std::string>  conflicts;

    // FIRST / nullable / FOLLOW
    std::map<NT, std::set<TokenType>> first;
    std::map<NT, bool>                nullable;
    std::map<NT, std::set<TokenType>> follow;

    bool built = false;
};

// --------------------------------------------------------------------------
// Grammar definition
// --------------------------------------------------------------------------
static void buildGrammar(LRTables& tb) {
    auto T = [](TokenType t) { return GSymbol::T(t); };
    auto N = [](NT n)        { return GSymbol::N(n); };
    auto add = [&](NT lhs, std::vector<GSymbol> rhs, std::string txt) {
        tb.prods.push_back({lhs, rhs, txt});
    };

    // Augmented start
    add(NT::S_PRIME, {N(NT::PROGRAM)}, "S' → program");

    // P1  program
    add(NT::PROGRAM,
        {T(TokenType::KW_PROGRAM), T(TokenType::ID),
         T(TokenType::LPAREN),     N(NT::IDENTIFIER_LIST),
         T(TokenType::RPAREN),     T(TokenType::SEMICOLON),
         N(NT::DECLARATIONS),      N(NT::SUBPROGRAM_DECLARATIONS),
         N(NT::COMPOUND_STATEMENT),T(TokenType::DOT)},
        "program → program id ( identifier_list ) ; declarations subprogram_declarations compound_statement .");

    // P2/P3  identifier_list
    add(NT::IDENTIFIER_LIST, {T(TokenType::ID)},
        "identifier_list → id");
    add(NT::IDENTIFIER_LIST,
        {N(NT::IDENTIFIER_LIST), T(TokenType::COMMA), T(TokenType::ID)},
        "identifier_list → identifier_list , id");

    // P4/P5  declarations
    add(NT::DECLARATIONS, {},
        "declarations → ε");
    add(NT::DECLARATIONS,
        {N(NT::DECLARATIONS), T(TokenType::KW_VAR),
         N(NT::IDENTIFIER_LIST), T(TokenType::COLON),
         N(NT::TYPE), T(TokenType::SEMICOLON)},
        "declarations → declarations var identifier_list : type ;");
    // Extra: allow continuation lines under same 'var' block
    // e.g.  var\n  a : integer ;\n  b : real ;
    // grammar: declarations → declarations identifier_list : type ;
    // (only valid after a var-started block; we accept it anywhere in
    //  declarations context — the symbol table handles the semantics)
    add(NT::DECLARATIONS,
        {N(NT::DECLARATIONS),
         N(NT::IDENTIFIER_LIST), T(TokenType::COLON),
         N(NT::TYPE), T(TokenType::SEMICOLON)},
        "declarations → declarations identifier_list : type ;" );

    // P6/P7  type
    add(NT::TYPE, {N(NT::STANDARD_TYPE)},
        "type → standard_type");
    add(NT::TYPE,
        {T(TokenType::KW_ARRAY), T(TokenType::LBRACKET),
         T(TokenType::NUM),      T(TokenType::DOTDOT),
         T(TokenType::NUM),      T(TokenType::RBRACKET),
         T(TokenType::KW_OF),    N(NT::STANDARD_TYPE)},
        "type → array [ num .. num ] of standard_type");

    // P8/P9  standard_type
    add(NT::STANDARD_TYPE, {T(TokenType::KW_INTEGER)}, "standard_type → integer");
    add(NT::STANDARD_TYPE, {T(TokenType::KW_REAL)},    "standard_type → real");

    // P10/P11  subprogram_declarations
    add(NT::SUBPROGRAM_DECLARATIONS, {},
        "subprogram_declarations → ε");
    add(NT::SUBPROGRAM_DECLARATIONS,
        {N(NT::SUBPROGRAM_DECLARATIONS), N(NT::SUBPROGRAM_DECLARATION),
         T(TokenType::SEMICOLON)},
        "subprogram_declarations → subprogram_declarations subprogram_declaration ;");

    // P12  subprogram_declaration
    add(NT::SUBPROGRAM_DECLARATION,
        {N(NT::SUBPROGRAM_HEAD), N(NT::DECLARATIONS), N(NT::COMPOUND_STATEMENT)},
        "subprogram_declaration → subprogram_head declarations compound_statement");

    // P13/P14  subprogram_head
    add(NT::SUBPROGRAM_HEAD,
        {T(TokenType::KW_FUNCTION), T(TokenType::ID),
         N(NT::ARGUMENTS),          T(TokenType::COLON),
         N(NT::STANDARD_TYPE),      T(TokenType::SEMICOLON)},
        "subprogram_head → function id arguments : standard_type ;");
    add(NT::SUBPROGRAM_HEAD,
        {T(TokenType::KW_PROCEDURE), T(TokenType::ID),
         N(NT::ARGUMENTS),           T(TokenType::SEMICOLON)},
        "subprogram_head → procedure id arguments ;");

    // P15/P16  arguments
    add(NT::ARGUMENTS, {},
        "arguments → ε");
    add(NT::ARGUMENTS,
        {T(TokenType::LPAREN), N(NT::PARAMETER_LIST), T(TokenType::RPAREN)},
        "arguments → ( parameter_list )");

    // P17/P18  parameter_list
    add(NT::PARAMETER_LIST,
        {N(NT::IDENTIFIER_LIST), T(TokenType::COLON), N(NT::TYPE)},
        "parameter_list → identifier_list : type");
    add(NT::PARAMETER_LIST,
        {N(NT::PARAMETER_LIST), T(TokenType::SEMICOLON),
         N(NT::IDENTIFIER_LIST), T(TokenType::COLON), N(NT::TYPE)},
        "parameter_list → parameter_list ; identifier_list : type");

    // P19  compound_statement
    add(NT::COMPOUND_STATEMENT,
        {T(TokenType::KW_BEGIN), N(NT::OPTIONAL_STATEMENTS), T(TokenType::KW_END)},
        "compound_statement → begin optional_statements end");

    // P20/P21  optional_statements
    add(NT::OPTIONAL_STATEMENTS, {},
        "optional_statements → ε");
    add(NT::OPTIONAL_STATEMENTS, {N(NT::STATEMENT_LIST)},
        "optional_statements → statement_list");

    // P22/P23  statement_list
    add(NT::STATEMENT_LIST, {N(NT::STATEMENT)},
        "statement_list → statement");
    add(NT::STATEMENT_LIST,
        {N(NT::STATEMENT_LIST), T(TokenType::SEMICOLON), N(NT::STATEMENT)},
        "statement_list → statement_list ; statement");

    // P24–P28  statement
    add(NT::STATEMENT,
        {N(NT::VARIABLE), T(TokenType::ASSIGNOP), N(NT::EXPRESSION)},
        "statement → variable := expression");
    add(NT::STATEMENT, {N(NT::PROCEDURE_STATEMENT)},
        "statement → procedure_statement");
    add(NT::STATEMENT, {N(NT::COMPOUND_STATEMENT)},
        "statement → compound_statement");
    add(NT::STATEMENT,
        {T(TokenType::KW_IF), N(NT::EXPRESSION), T(TokenType::KW_THEN),
         N(NT::STATEMENT),    T(TokenType::KW_ELSE), N(NT::STATEMENT)},
        "statement → if expression then statement else statement");
    add(NT::STATEMENT,
        {T(TokenType::KW_WHILE), N(NT::EXPRESSION),
         T(TokenType::KW_DO),    N(NT::STATEMENT)},
        "statement → while expression do statement");

    // P29/P30  variable
    add(NT::VARIABLE, {T(TokenType::ID)},
        "variable → id");
    add(NT::VARIABLE,
        {T(TokenType::ID), T(TokenType::LBRACKET),
         N(NT::EXPRESSION), T(TokenType::RBRACKET)},
        "variable → id [ expression ]");

    // P31/P32  procedure_statement
    add(NT::PROCEDURE_STATEMENT, {T(TokenType::ID)},
        "procedure_statement → id");
    add(NT::PROCEDURE_STATEMENT,
        {T(TokenType::ID), T(TokenType::LPAREN),
         N(NT::EXPRESSION_LIST), T(TokenType::RPAREN)},
        "procedure_statement → id ( expression_list )");

    // P33/P34  expression_list
    add(NT::EXPRESSION_LIST, {N(NT::EXPRESSION)},
        "expression_list → expression");
    add(NT::EXPRESSION_LIST,
        {N(NT::EXPRESSION_LIST), T(TokenType::COMMA), N(NT::EXPRESSION)},
        "expression_list → expression_list , expression");

    // P35/P36  expression
    add(NT::EXPRESSION, {N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_EQ),  N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression = simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_NEQ), N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression <> simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_LT),  N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression < simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_LE),  N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression <= simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_GE),  N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression >= simple_expression");
    add(NT::EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::RELOP_GT),  N(NT::SIMPLE_EXPRESSION)},
        "expression → simple_expression > simple_expression");

    // P37–P39  simple_expression
    add(NT::SIMPLE_EXPRESSION, {N(NT::TERM)},
        "simple_expression → term");
    add(NT::SIMPLE_EXPRESSION, {N(NT::SIGN), N(NT::TERM)},
        "simple_expression → sign term");
    add(NT::SIMPLE_EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::ADDOP_PLUS), N(NT::TERM)},
        "simple_expression → simple_expression + term");
    add(NT::SIMPLE_EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::ADDOP_MINUS), N(NT::TERM)},
        "simple_expression → simple_expression - term");
    add(NT::SIMPLE_EXPRESSION,
        {N(NT::SIMPLE_EXPRESSION), T(TokenType::KW_OR), N(NT::TERM)},
        "simple_expression → simple_expression or term");

    // P40–P41  term
    add(NT::TERM, {N(NT::FACTOR)},
        "term → factor");
    add(NT::TERM,
        {N(NT::TERM), T(TokenType::MULOP_STAR), N(NT::FACTOR)},
        "term → term * factor");
    add(NT::TERM,
        {N(NT::TERM), T(TokenType::MULOP_SLASH), N(NT::FACTOR)},
        "term → term / factor");
    add(NT::TERM,
        {N(NT::TERM), T(TokenType::KW_DIV), N(NT::FACTOR)},
        "term → term div factor");
    add(NT::TERM,
        {N(NT::TERM), T(TokenType::KW_MOD), N(NT::FACTOR)},
        "term → term mod factor");
    add(NT::TERM,
        {N(NT::TERM), T(TokenType::KW_AND), N(NT::FACTOR)},
        "term → term and factor");

    // P42–P46  factor
    add(NT::FACTOR, {T(TokenType::ID)},
        "factor → id");
    add(NT::FACTOR,
        {T(TokenType::ID), T(TokenType::LPAREN),
         N(NT::EXPRESSION_LIST), T(TokenType::RPAREN)},
        "factor → id ( expression_list )");
    add(NT::FACTOR,
        {T(TokenType::ID), T(TokenType::LBRACKET),
         N(NT::EXPRESSION), T(TokenType::RBRACKET)},
        "factor → id [ expression ]");
    add(NT::FACTOR, {T(TokenType::NUM)},
        "factor → num");
    add(NT::FACTOR,
        {T(TokenType::LPAREN), N(NT::EXPRESSION), T(TokenType::RPAREN)},
        "factor → ( expression )");
    add(NT::FACTOR, {T(TokenType::KW_NOT), N(NT::FACTOR)},
        "factor → not factor");

    // P47/P48  sign
    add(NT::SIGN, {T(TokenType::ADDOP_PLUS)},  "sign → +");
    add(NT::SIGN, {T(TokenType::ADDOP_MINUS)}, "sign → -");
}

// --------------------------------------------------------------------------
// FIRST / nullable
// --------------------------------------------------------------------------
static void computeFirst(LRTables& tb) {
    for (int i = 0; i < NT_COUNT; ++i) {
        tb.nullable[(NT)i] = false;
        tb.first[(NT)i] = {};
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& p : tb.prods) {
            bool allNull = true;
            for (auto& sym : p.rhs) {
                if (sym.kind == GSymbol::TERM) {
                    if (tb.first[p.lhs].insert(sym.tt).second) changed = true;
                    allNull = false;
                    break;
                }
                // sym is NT
                for (auto t : tb.first[sym.nt])
                    if (tb.first[p.lhs].insert(t).second) changed = true;
                if (!tb.nullable[sym.nt]) { allNull = false; break; }
            }
            if (allNull && !tb.nullable[p.lhs]) {
                tb.nullable[p.lhs] = true;
                changed = true;
            }
        }
    }
}

// --------------------------------------------------------------------------
// FOLLOW
// --------------------------------------------------------------------------
static void computeFollow(LRTables& tb) {
    for (int i = 0; i < NT_COUNT; ++i) tb.follow[(NT)i] = {};
    tb.follow[NT::S_PRIME].insert(TokenType::EOF_TOKEN);

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& p : tb.prods) {
            for (int i = 0; i < (int)p.rhs.size(); ++i) {
                if (p.rhs[i].kind != GSymbol::NONTERM) continue;
                NT B = p.rhs[i].nt;

                // FIRST of the suffix β after B
                bool betaNull = true;
                for (int j = i+1; j < (int)p.rhs.size(); ++j) {
                    auto& s = p.rhs[j];
                    if (s.kind == GSymbol::TERM) {
                        if (tb.follow[B].insert(s.tt).second) changed = true;
                        betaNull = false;
                        break;
                    }
                    for (auto t : tb.first[s.nt])
                        if (tb.follow[B].insert(t).second) changed = true;
                    if (!tb.nullable[s.nt]) { betaNull = false; break; }
                }
                if (betaNull)
                    for (auto t : tb.follow[p.lhs])
                        if (tb.follow[B].insert(t).second) changed = true;
            }
        }
    }
}

// --------------------------------------------------------------------------
// LR(0) closure
// --------------------------------------------------------------------------
static ItemSet closure(const ItemSet& I, const std::vector<Production>& prods) {
    ItemSet J = I;
    bool changed = true;
    while (changed) {
        changed = false;
        ItemSet add;
        for (auto& it : J) {
            auto& p = prods[it.prodIdx];
            if (it.dot >= (int)p.rhs.size()) continue;
            auto& sym = p.rhs[it.dot];
            if (sym.kind != GSymbol::NONTERM) continue;
            NT B = sym.nt;
            for (int i = 0; i < (int)prods.size(); ++i) {
                if (prods[i].lhs == B) {
                    Item ni{i, 0};
                    if (J.find(ni) == J.end() && add.find(ni) == add.end()) {
                        add.insert(ni);
                        changed = true;
                    }
                }
            }
        }
        J.insert(add.begin(), add.end());
    }
    return J;
}

// --------------------------------------------------------------------------
// LR(0) goto
// --------------------------------------------------------------------------
static ItemSet doGoto(const ItemSet& I,
                      const GSymbol& X,
                      const std::vector<Production>& prods) {
    ItemSet J;
    for (auto& it : I) {
        auto& p = prods[it.prodIdx];
        if (it.dot < (int)p.rhs.size() && p.rhs[it.dot] == X)
            J.insert({it.prodIdx, it.dot + 1});
    }
    return closure(J, prods);
}

// --------------------------------------------------------------------------
// Build canonical collection
// --------------------------------------------------------------------------
static void buildCanonical(LRTables& tb) {
    ItemSet start = closure({{0, 0}}, tb.prods);
    tb.states.push_back(start);

    std::queue<int> wl;
    wl.push(0);

    while (!wl.empty()) {
        int i = wl.front(); wl.pop();

        // Collect all symbols that appear after a dot in this state
        std::set<GSymbol> syms;
        for (auto& it : tb.states[i]) {
            auto& p = tb.prods[it.prodIdx];
            if (it.dot < (int)p.rhs.size())
                syms.insert(p.rhs[it.dot]);
        }

        for (auto& X : syms) {
            ItemSet G = doGoto(tb.states[i], X, tb.prods);
            if (G.empty()) continue;

            // Find or create target state
            int target = -1;
            for (int j = 0; j < (int)tb.states.size(); ++j) {
                if (tb.states[j] == G) { target = j; break; }
            }
            if (target < 0) {
                tb.states.push_back(G);
                target = (int)tb.states.size() - 1;
                wl.push(target);
            }

            if (X.kind == GSymbol::TERM)
                tb.shiftGoto[{i, X.tt}] = target;
            else
                tb.gotoTab[{i, X.nt}] = target;
        }
    }
}

// --------------------------------------------------------------------------
// Build ACTION table  (SLR(1))
// --------------------------------------------------------------------------
static void buildAction(LRTables& tb) {
    // Pass 1: shifts and accept
    for (int i = 0; i < (int)tb.states.size(); ++i) {
        for (auto& it : tb.states[i]) {
            auto& p = tb.prods[it.prodIdx];
            if (it.dot < (int)p.rhs.size()) {
                auto& sym = p.rhs[it.dot];
                if (sym.kind == GSymbol::TERM) {
                    auto k = std::make_pair(i, sym.tt);
                    auto sg = tb.shiftGoto.find(k);
                    if (sg != tb.shiftGoto.end())
                        tb.actionTab[{i, sym.tt}] = {AKind::SHIFT, sg->second};
                }
            } else if (p.lhs == NT::S_PRIME) {
                tb.actionTab[{i, TokenType::EOF_TOKEN}] = {AKind::ACCEPT, 0};
            }
        }
    }

    // Pass 2: reduces — only if no shift already set (shift wins, handles dangling-else)
    for (int i = 0; i < (int)tb.states.size(); ++i) {
        for (auto& it : tb.states[i]) {
            auto& p = tb.prods[it.prodIdx];
            if (it.dot != (int)p.rhs.size()) continue;
            if (p.lhs == NT::S_PRIME) continue;

            for (auto a : tb.follow[p.lhs]) {
                auto key = std::make_pair(i, a);
                auto existing = tb.actionTab.find(key);
                if (existing != tb.actionTab.end()) {
                    if (existing->second.kind == AKind::SHIFT) {
                        // Shift/reduce: prefer shift (dangling-else resolution)
                        std::ostringstream oss;
                        oss << "State " << i << " on '" << tokenTypeToString(a)
                            << "': shift/reduce conflict → shift wins (prod "
                            << it.prodIdx << ": " << p.text << ")";
                        tb.conflicts.push_back(oss.str());
                    }
                    // else reduce/reduce: keep first (earlier production wins)
                } else {
                    tb.actionTab[key] = {AKind::REDUCE, it.prodIdx};
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// One-time build
// --------------------------------------------------------------------------
static LRTables& getTables() {
    static LRTables tb;
    if (!tb.built) {
        buildGrammar(tb);
        computeFirst(tb);
        computeFollow(tb);
        buildCanonical(tb);
        buildAction(tb);
        tb.built = true;
    }
    return tb;
}

// ============================================================================
//  Semantic actions
// ============================================================================
struct SemCtx {
    SymbolTable&             sym;
    std::vector<std::string> pendingIds;
    SymType                  pendingType  = SymType::TYPE_INTEGER;
    int                      arrLo = 0, arrHi = 0;
    int                      lastLine = 1;
    // Parameters collected during parameter_list reduces — flushed into the
    // new scope AFTER enterScope() fires in subprogram_head reduce.
    struct PendingParam { std::string name; SymType type; int line; };
    std::vector<PendingParam> pendingParams;
};

static void onReduce(int prodIdx,
                     const std::vector<GSymbol>& popped,
                     SemCtx& sc,
                     const std::vector<Production>& prods)
{
    const Production& p = prods[prodIdx];

    // identifier_list → id
    if (p.lhs == NT::IDENTIFIER_LIST && (int)p.rhs.size() == 1) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM)
            sc.pendingIds.push_back(popped[0].tok.lexeme);
    }
    // identifier_list → identifier_list , id
    else if (p.lhs == NT::IDENTIFIER_LIST && (int)p.rhs.size() == 3) {
        if (!popped.empty() && popped[2].kind == GSymbol::TERM)
            sc.pendingIds.push_back(popped[2].tok.lexeme);
    }
    // standard_type → integer
    else if (p.lhs == NT::STANDARD_TYPE && !p.rhs.empty()
             && p.rhs[0].kind == GSymbol::TERM
             && p.rhs[0].tt == TokenType::KW_INTEGER) {
        sc.pendingType = SymType::TYPE_INTEGER;
    }
    // standard_type → real
    else if (p.lhs == NT::STANDARD_TYPE && !p.rhs.empty()
             && p.rhs[0].kind == GSymbol::TERM
             && p.rhs[0].tt == TokenType::KW_REAL) {
        sc.pendingType = SymType::TYPE_REAL;
    }
    // type → array [ num .. num ] of standard_type
    else if (p.lhs == NT::TYPE && (int)p.rhs.size() == 8) {
        sc.pendingType = SymType::TYPE_ARRAY;
        if ((int)popped.size() >= 8) {
            if (popped[2].kind == GSymbol::TERM)
                try { sc.arrLo = std::stoi(popped[2].tok.lexeme); } catch(...) {}
            if (popped[4].kind == GSymbol::TERM)
                try { sc.arrHi = std::stoi(popped[4].tok.lexeme); } catch(...) {}
        }
    }
    // declarations → declarations var identifier_list : type ;   (rhs.size()==6)
    // declarations → declarations identifier_list : type ;        (rhs.size()==5, var-block continuation)
    else if (p.lhs == NT::DECLARATIONS &&
             ((int)p.rhs.size() == 6 || (int)p.rhs.size() == 5)) {
        for (const auto& nm : sc.pendingIds) {
            SymbolEntry e;
            e.name        = nm;
            e.kind        = SymbolKind::VARIABLE;
            e.type        = sc.pendingType;
            e.scope_level = sc.sym.currentScope();
            e.line        = sc.lastLine;
            e.array_start = sc.arrLo;
            e.array_end   = sc.arrHi;
            sc.sym.insert(e);
        }
        sc.pendingIds.clear();
    }    // parameter_list → identifier_list : type   (rhs.size()==3)
    // — collect params into pendingParams; actual insert happens after enterScope
    else if (p.lhs == NT::PARAMETER_LIST && (int)p.rhs.size() == 3) {
        for (const auto& nm : sc.pendingIds)
            sc.pendingParams.push_back({nm, sc.pendingType, sc.lastLine});
        sc.pendingIds.clear();
    }
    // parameter_list → parameter_list ; identifier_list : type   (rhs.size()==5)
    else if (p.lhs == NT::PARAMETER_LIST && (int)p.rhs.size() == 5) {
        for (const auto& nm : sc.pendingIds)
            sc.pendingParams.push_back({nm, sc.pendingType, sc.lastLine});
        sc.pendingIds.clear();
    }
    // subprogram_head → function id arguments : standard_type ;
    else if (p.lhs == NT::SUBPROGRAM_HEAD && (int)p.rhs.size() == 6) {
        // Insert the function name in the CURRENT (outer) scope
        if ((int)popped.size() >= 6 && popped[1].kind == GSymbol::TERM) {
            SymbolEntry e;
            e.name        = popped[1].tok.lexeme;
            e.kind        = SymbolKind::FUNCTION;
            e.type        = sc.pendingType;
            e.scope_level = sc.sym.currentScope();
            e.line        = popped[1].tok.line;
            sc.sym.insert(e);
        }
        // Open new scope, then flush collected parameters into it
        sc.sym.enterScope();
        for (auto& pp : sc.pendingParams) {
            SymbolEntry e;
            e.name        = pp.name;
            e.kind        = SymbolKind::PARAMETER;
            e.type        = pp.type;
            e.scope_level = sc.sym.currentScope();
            e.line        = pp.line;
            sc.sym.insert(e);
        }
        sc.pendingParams.clear();
    }
    // subprogram_head → procedure id arguments ;
    else if (p.lhs == NT::SUBPROGRAM_HEAD && (int)p.rhs.size() == 4) {
        if ((int)popped.size() >= 4 && popped[1].kind == GSymbol::TERM) {
            SymbolEntry e;
            e.name        = popped[1].tok.lexeme;
            e.kind        = SymbolKind::PROCEDURE;
            e.type        = SymType::TYPE_VOID;
            e.scope_level = sc.sym.currentScope();
            e.line        = popped[1].tok.line;
            sc.sym.insert(e);
        }
        // Open new scope, flush params
        sc.sym.enterScope();
        for (auto& pp : sc.pendingParams) {
            SymbolEntry e;
            e.name        = pp.name;
            e.kind        = SymbolKind::PARAMETER;
            e.type        = pp.type;
            e.scope_level = sc.sym.currentScope();
            e.line        = pp.line;
            sc.sym.insert(e);
        }
        sc.pendingParams.clear();
    }
    // factor → id   (plain variable use — undeclared check)
    else if (p.lhs == NT::FACTOR && (int)p.rhs.size() == 1
             && !p.rhs.empty() && p.rhs[0].kind == GSymbol::TERM
             && p.rhs[0].tt == TokenType::ID) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            if (!sc.sym.lookup(nm))
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
        }
    }
    // factor → id ( expression_list )  or  factor → id [ expression ]
    else if (p.lhs == NT::FACTOR && (int)p.rhs.size() == 4
             && !p.rhs.empty() && p.rhs[0].kind == GSymbol::TERM
             && p.rhs[0].tt == TokenType::ID) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            if (!sc.sym.lookup(nm))
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
        }
    }
    // variable → id   (assignment lvalue)
    else if (p.lhs == NT::VARIABLE && (int)p.rhs.size() == 1) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            if (!sc.sym.lookup(nm))
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
        }
    }
    // variable → id [ expression ]   (array element lvalue)
    else if (p.lhs == NT::VARIABLE && (int)p.rhs.size() == 4) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            const SymbolEntry* e = sc.sym.lookup(nm);
            if (!e)
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
            else if (e->type != SymType::TYPE_ARRAY)
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "'" + nm + "' is not an array (subscript on non-array)");
        }
    }
    // procedure_statement → id   (undeclared procedure call)
    else if (p.lhs == NT::PROCEDURE_STATEMENT && (int)p.rhs.size() == 1) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            if (!sc.sym.lookup(nm))
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
        }
    }
    // procedure_statement → id ( expression_list )
    else if (p.lhs == NT::PROCEDURE_STATEMENT && (int)p.rhs.size() == 4) {
        if (!popped.empty() && popped[0].kind == GSymbol::TERM) {
            const std::string& nm = popped[0].tok.lexeme;
            const SymbolEntry* e = sc.sym.lookup(nm);
            if (!e)
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "Undeclared identifier '" + nm + "'");
            else if (e->kind != SymbolKind::PROCEDURE
                  && e->kind != SymbolKind::FUNCTION)
                ErrorHandler::instance().semError(
                    popped[0].tok.line, popped[0].tok.col,
                    "'" + nm + "' is not a procedure");
        }
    }
    // subprogram_declaration → subprogram_head declarations compound_statement
    else if (p.lhs == NT::SUBPROGRAM_DECLARATION) {
        sc.sym.exitScope();
    }
}

// ============================================================================
//  Trace helpers
// ============================================================================
static std::string stateStackStr(const std::vector<int>& ss) {
    std::string r;
    int start = (int)ss.size() > 6 ? (int)ss.size() - 6 : 0;
    if (start > 0) r = "...";
    for (int i = start; i < (int)ss.size(); ++i) {
        r += std::to_string(ss[i]);
        if (i+1 < (int)ss.size()) r += " ";
    }
    return r;
}

static std::string symStackStr(const std::vector<GSymbol>& ss) {
    // skip bottom $ sentinel
    int start = (int)ss.size() > 5 ? (int)ss.size() - 5 : 1;
    std::string r;
    if (start > 1) r = "... ";
    for (int i = start; i < (int)ss.size(); ++i) {
        if (ss[i].kind == GSymbol::TERM && !ss[i].tok.lexeme.empty())
            r += ss[i].tok.lexeme;
        else
            r += ss[i].str();
        r += " ";
    }
    return r.empty() ? "$" : r;
}

static std::string inputStr(const std::vector<Token>& toks, int pos) {
    std::string r;
    int lim = std::min((int)toks.size(), pos + 4);
    for (int i = pos; i < lim; ++i) {
        if (toks[i].type == TokenType::EOF_TOKEN) { r += "$"; break; }
        r += toks[i].lexeme;
        if (i+1 < lim) r += " ";
    }
    if ((int)toks.size() - pos > 4) r += " ...";
    return r.empty() ? "$" : r;
}

// ============================================================================
//  ParserLR public interface
// ============================================================================
ParserLR::ParserLR(Lexer& lexer, SymbolTable& symTable)
    : lexer_(lexer), sym_(symTable)
{}

bool ParserLR::parse() {
    if (g_verbose) traceMode_ = true;

    // Build tables once (cached across calls)
    LRTables& tb = getTables();

    // ── Collect all tokens
    std::vector<Token> tokens;
    {
        Token t;
        do {
            t = lexer_.nextToken();
            tokens.push_back(t);
        } while (t.type != TokenType::EOF_TOKEN);
    }

    // ── Two explicit stacks
    std::vector<int>     stateStack;
    std::vector<GSymbol> symStack;

    stateStack.push_back(0);
    // bottom-of-stack sentinel
    GSymbol bot = GSymbol::T(TokenType::EOF_TOKEN);
    symStack.push_back(bot);

    // ── Semantic context
    SemCtx sc{sym_, {}, SymType::TYPE_INTEGER, 0, 0, 1, {}};

    // ── Token cursor
    int pos = 0;
    auto cur = [&]() -> const Token& { return tokens[pos]; };

    // ── Trace header
    if (traceMode_) {
        std::cout << "\n" << std::string(110,'=') << "\n"
                  << "  SLR(1) Parse Trace  ("
                  << tb.states.size() << " states, "
                  << tb.prods.size()  << " productions)\n"
                  << std::string(110,'=') << "\n";
        std::cout << std::left
                  << std::setw(5)  << "Step"
                  << std::setw(22) << "State Stack"
                  << std::setw(30) << "Symbol Stack"
                  << std::setw(24) << "Remaining Input"
                  << "Action\n";
        std::cout << std::string(110,'-') << "\n";
    }

    bool accepted = false;
    int  step     = 0;
    bool fatal    = false;

    // Sync tokens for panic-mode recovery
    auto isSync = [](TokenType t) {
        return t == TokenType::SEMICOLON ||
               t == TokenType::KW_END   ||
               t == TokenType::KW_BEGIN ||
               t == TokenType::DOT      ||
               t == TokenType::EOF_TOKEN;
    };

    while (!fatal) {
        int s = stateStack.back();
        TokenType a = cur().type;

        auto actIt = tb.actionTab.find({s, a});

        // ── Trace row
        if (traceMode_) {
            ++step;
            std::string actStr;
            if (actIt == tb.actionTab.end()) {
                actStr = "error";
            } else {
                switch (actIt->second.kind) {
                    case AKind::SHIFT:
                        actStr = "shift  " + std::to_string(actIt->second.value);
                        break;
                    case AKind::REDUCE: {
                        int pi = actIt->second.value;
                        actStr = "reduce P" + std::to_string(pi)
                               + " (" + tb.prods[pi].text + ")";
                        break;
                    }
                    case AKind::ACCEPT:
                        actStr = "accept";
                        break;
                }
            }
            std::cout << std::left
                      << std::setw(5)  << step
                      << std::setw(22) << stateStackStr(stateStack)
                      << std::setw(30) << symStackStr(symStack)
                      << std::setw(24) << inputStr(tokens, pos)
                      << actStr << "\n";
        }

        // ── Error
        if (actIt == tb.actionTab.end()) {
            hadError_ = true;
            std::ostringstream msg;
            msg << "Unexpected '" << cur().lexeme
                << "' (" << tokenTypeToString(a)
                << ") in state " << s;
            ErrorHandler::instance().synError(cur().line, cur().col, msg.str());

            // Panic-mode: skip to sync token
            while (!isSync(cur().type)) {
                ++pos;
                if (pos >= (int)tokens.size()) { fatal = true; break; }
            }
            if (fatal) break;

            // Pop states until current sync token has an action
            TokenType nxt = cur().type;
            while (stateStack.size() > 1) {
                int top = stateStack.back();
                if (tb.actionTab.count({top, nxt})) break;
                stateStack.pop_back();
                symStack.pop_back();
            }
            if (!tb.actionTab.count({stateStack.back(), nxt}))
                fatal = true;
            continue;
        }

        Action act = actIt->second;

        // ── Accept
        if (act.kind == AKind::ACCEPT) {
            accepted = true;
            if (traceMode_)
                std::cout << std::string(110,'-') << "\n"
                          << "  ✓ Input accepted.\n"
                          << std::string(110,'=') << "\n\n";
            break;
        }

        // ── Shift
        if (act.kind == AKind::SHIFT) {
            sc.lastLine = cur().line;
            GSymbol gs = GSymbol::T(a);
            gs.tok = cur();
            symStack.push_back(gs);
            stateStack.push_back(act.value);
            ++pos;
        }
        // ── Reduce
        else {
            int pi = act.value;
            const Production& prod = tb.prods[pi];
            int len = (int)prod.rhs.size();

            // Collect popped symbols (for semantic actions)
            std::vector<GSymbol> popped(len);
            for (int i = len-1; i >= 0; --i) {
                if (stateStack.size() <= 1 || symStack.empty()) {
                    fatal = true; break;
                }
                popped[i] = symStack.back();
                symStack.pop_back();
                stateStack.pop_back();
            }
            if (fatal) break;

            // Fire semantic action
            onReduce(pi, popped, sc, tb.prods);

            // Push LHS non-terminal
            symStack.push_back(GSymbol::N(prod.lhs));

            // GOTO
            int topSt = stateStack.back();
            auto gt = tb.gotoTab.find({topSt, prod.lhs});
            if (gt == tb.gotoTab.end()) {
                if (prod.lhs == NT::S_PRIME) { accepted = true; break; }
                hadError_ = true;
                ErrorHandler::instance().synError(
                    cur().line, cur().col,
                    "GOTO error: no entry for " + ntStr(prod.lhs)
                    + " from state " + std::to_string(topSt));
                fatal = true;
                break;
            }
            stateStack.push_back(gt->second);
        }
    }

    hadError_ = hadError_ || ErrorHandler::instance().hasErrors();
    return accepted && !hadError_;
}

// ============================================================================
//  printActionTable
// ============================================================================
void ParserLR::printActionTable() const {
    LRTables& tb = getTables();

    // Collect all terminals that appear in the table
    std::set<TokenType> usedT;
    for (auto& kv : tb.actionTab) usedT.insert(kv.first.second);
    std::vector<TokenType> cols(usedT.begin(), usedT.end());

    std::cout << "\n=== ACTION TABLE  (" << tb.states.size()
              << " states) ===\n";
    std::cout << "  s<n>=shift n   r<n>=reduce by prod n   acc=accept   .=error\n\n";

    // Header
    std::cout << std::left << std::setw(5) << "St";
    for (auto t : cols)
        std::cout << std::setw(7) << tokenTypeToString(t).substr(0,6);
    std::cout << "\n" << std::string(5 + 7*(int)cols.size(), '-') << "\n";

    for (int s = 0; s < (int)tb.states.size(); ++s) {
        std::cout << std::setw(5) << s;
        for (auto t : cols) {
            auto it = tb.actionTab.find({s, t});
            std::string cell;
            if (it == tb.actionTab.end()) cell = ".";
            else if (it->second.kind == AKind::SHIFT)
                cell = "s" + std::to_string(it->second.value);
            else if (it->second.kind == AKind::REDUCE)
                cell = "r" + std::to_string(it->second.value);
            else cell = "acc";
            std::cout << std::setw(7) << cell;
        }
        std::cout << "\n";
    }
    std::cout << "\n";

    if (!tb.conflicts.empty()) {
        std::cout << "Conflicts resolved (" << tb.conflicts.size() << "):\n";
        for (auto& c : tb.conflicts) std::cout << "  " << c << "\n";
        std::cout << "\n";
    }
}

// ============================================================================
//  printGotoTable
// ============================================================================
void ParserLR::printGotoTable() const {
    LRTables& tb = getTables();

    // Collect non-terminals that appear in goto (skip S')
    std::set<NT> usedNT;
    for (auto& kv : tb.gotoTab)
        if (kv.first.second != NT::S_PRIME) usedNT.insert(kv.first.second);
    std::vector<NT> cols(usedNT.begin(), usedNT.end());

    std::cout << "\n=== GOTO TABLE  (" << tb.states.size()
              << " states) ===\n\n";

    std::cout << std::left << std::setw(5) << "St";
    for (auto n : cols)
        std::cout << std::setw(10) << ntStr(n).substr(0,9);
    std::cout << "\n" << std::string(5 + 10*(int)cols.size(), '-') << "\n";

    for (int s = 0; s < (int)tb.states.size(); ++s) {
        std::cout << std::setw(5) << s;
        for (auto n : cols) {
            auto it = tb.gotoTab.find({s, n});
            std::string cell = (it == tb.gotoTab.end()) ? "."
                                                        : std::to_string(it->second);
            std::cout << std::setw(10) << cell;
        }
        std::cout << "\n";
    }
    std::cout << "\n";
}
