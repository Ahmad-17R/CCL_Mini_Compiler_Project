// =============================================================================
// symtable_test.cpp
// Standalone unit tests for SymbolTable.
// Build:
//   g++ -std=c++17 -Wall -g -o symtable_test
//       test/symtable_test.cpp
//       src/symtable/symbol_table.cpp
//       src/errorhandler/error_handler.cpp
// Run:
//   ./symtable_test
// =============================================================================

#include "../src/symtable/symbol_table.h"
#include "../src/errorhandler/error_handler.h"
#include "../src/common/globals.h"

#include <iostream>
#include <string>
#include <cassert>

// Required globals
ParserMode  g_parserMode = ParserMode::RD;
std::string g_sourceFile = "<test>";
bool        g_verbose    = false;
int         g_errorCount = 0;

// ---- tiny test harness ------------------------------------------------------
static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "  PASS  " << (msg) << "\n"; \
            ++passed; \
        } else { \
            std::cout << "  FAIL  " << (msg) \
                      << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; \
            ++failed; \
        } \
    } while (0)

// =============================================================================
static void test_basic_insert_lookup() {
    std::cout << "\n[TEST] Basic insert / lookup\n";
    SymbolTable st;

    SymbolEntry e;
    e.name  = "x";
    e.kind  = SymbolKind::VARIABLE;
    e.type  = SymType::TYPE_INTEGER;
    e.line  = 10;
    e.col   = 5;

    bool ok = st.insert(e);
    CHECK(ok, "insert 'x' into global scope succeeds");

    SymbolEntry* p = st.lookup("x");
    CHECK(p != nullptr, "lookup 'x' returns non-null");
    CHECK(p->name == "x", "lookup 'x' -> name == 'x'");
    CHECK(p->kind == SymbolKind::VARIABLE, "lookup 'x' -> kind == VARIABLE");
    CHECK(p->type == SymType::TYPE_INTEGER, "lookup 'x' -> type == TYPE_INTEGER");
    CHECK(p->line == 10, "lookup 'x' -> line == 10");
    CHECK(p->scope_level == 0, "lookup 'x' -> scope_level == 0");

    // Case-insensitivity
    SymbolEntry* p2 = st.lookup("X");
    CHECK(p2 != nullptr, "lookup 'X' (uppercase) also finds 'x'");

    // Not-found
    SymbolEntry* p3 = st.lookup("y");
    CHECK(p3 == nullptr, "lookup 'y' (not inserted) returns nullptr");
}

// =============================================================================
static void test_duplicate_in_same_scope() {
    std::cout << "\n[TEST] Duplicate in same scope\n";
    SymbolTable st;

    SymbolEntry e;
    e.name = "alpha";
    e.kind = SymbolKind::VARIABLE;
    e.type = SymType::TYPE_REAL;
    e.line = 1; e.col = 1;
    st.insert(e);

    // Insert the same name again — must fail and report an error
    int errorsBefore = ErrorHandler::instance().errorCount();
    e.line = 2;
    bool ok2 = st.insert(e);
    int errorsAfter = ErrorHandler::instance().errorCount();

    CHECK(!ok2, "duplicate insert returns false");
    CHECK(errorsAfter == errorsBefore + 1,
          "duplicate insert increments error count by 1");
}

// =============================================================================
static void test_scoping_and_shadowing() {
    std::cout << "\n[TEST] Scoping and shadowing\n";
    SymbolTable st;

    // Insert 'n' at global scope
    SymbolEntry global;
    global.name  = "n";
    global.kind  = SymbolKind::VARIABLE;
    global.type  = SymType::TYPE_INTEGER;
    global.line  = 1; global.col = 1;
    st.insert(global);

    st.enterScope(); // scope 1

    // Insert 'n' again at scope 1 — shadows global
    SymbolEntry local;
    local.name  = "n";
    local.kind  = SymbolKind::PARAMETER;
    local.type  = SymType::TYPE_REAL;
    local.line  = 5; local.col = 1;
    st.insert(local);

    // lookup should find the inner 'n'
    SymbolEntry* p = st.lookup("n");
    CHECK(p != nullptr, "lookup 'n' inside scope 1 returns non-null");
    CHECK(p->scope_level == 1, "lookup finds inner 'n' (scope 1)");
    CHECK(p->type == SymType::TYPE_REAL, "inner 'n' has type REAL");

    // lookupCurrentScope should find inner 'n'
    SymbolEntry* pc = st.lookupCurrentScope("n");
    CHECK(pc != nullptr, "lookupCurrentScope finds 'n' at scope 1");

    st.exitScope(); // back to scope 0

    // Now lookup should find the global 'n' again
    SymbolEntry* pg = st.lookup("n");
    CHECK(pg != nullptr, "after exitScope, lookup 'n' returns non-null");
    CHECK(pg->scope_level == 0, "after exitScope, finds global 'n' (scope 0)");
    CHECK(pg->type == SymType::TYPE_INTEGER, "global 'n' has type INTEGER");

    // lookupCurrentScope should NOT find inner 'n' anymore
    SymbolEntry* pcs2 = st.lookupCurrentScope("n");
    CHECK(pcs2 != nullptr, "lookupCurrentScope at scope 0 still finds global 'n'");
}

// =============================================================================
static void test_multiple_scopes() {
    std::cout << "\n[TEST] Multiple scopes / nested functions\n";
    SymbolTable st;

    // program name at scope 0
    SymbolEntry prog;
    prog.name = "myprogram"; prog.kind = SymbolKind::PROGRAM;
    prog.type = SymType::TYPE_VOID; prog.line = 1; prog.col = 9;
    st.insert(prog);

    // global vars
    SymbolEntry gv;
    gv.name = "g"; gv.kind = SymbolKind::VARIABLE;
    gv.type = SymType::TYPE_INTEGER; gv.line = 3; gv.col = 5;
    st.insert(gv);

    st.enterScope(); // scope 1 — function foo

    SymbolEntry fn;
    fn.name = "foo"; fn.kind = SymbolKind::FUNCTION;
    fn.type = SymType::TYPE_INTEGER; fn.param_count = 2;
    fn.line = 6; fn.col = 10;
    st.insert(fn);

    SymbolEntry p1; p1.name="a"; p1.kind=SymbolKind::PARAMETER;
    p1.type=SymType::TYPE_INTEGER; p1.line=6; p1.col=14;
    st.insert(p1);

    SymbolEntry p2; p2.name="b"; p2.kind=SymbolKind::PARAMETER;
    p2.type=SymType::TYPE_INTEGER; p2.line=6; p2.col=17;
    st.insert(p2);

    st.enterScope(); // scope 2 — body of foo

    SymbolEntry lv; lv.name="tmp"; lv.kind=SymbolKind::VARIABLE;
    lv.type=SymType::TYPE_REAL; lv.line=8; lv.col=9;
    st.insert(lv);

    // can see everything from enclosing scopes
    CHECK(st.lookup("g")   != nullptr, "inner scope sees global 'g'");
    CHECK(st.lookup("foo") != nullptr, "inner scope sees 'foo' from scope 1");
    CHECK(st.lookup("a")   != nullptr, "inner scope sees parameter 'a'");
    CHECK(st.lookup("tmp") != nullptr, "inner scope sees local 'tmp'");
    CHECK(st.currentScope() == 2,      "currentScope() == 2 inside foo body");

    st.exitScope(); // leave foo body, scope 1 restored
    CHECK(st.lookup("tmp") == nullptr, "'tmp' gone after exiting foo body");
    CHECK(st.lookup("a")   != nullptr, "'a' still visible at scope 1");

    st.exitScope(); // back to global
    CHECK(st.lookup("a")   == nullptr, "'a' gone after exiting foo scope");
    CHECK(st.lookup("g")   != nullptr, "global 'g' still visible");
    CHECK(st.currentScope() == 0,      "currentScope() == 0 at global");
}

// =============================================================================
static void test_array_entry() {
    std::cout << "\n[TEST] Array entries\n";
    SymbolTable st;

    SymbolEntry arr;
    arr.name        = "table";
    arr.kind        = SymbolKind::ARRAY;
    arr.type        = SymType::TYPE_ARRAY;
    arr.array_start = 1;
    arr.array_end   = 50;
    arr.line        = 4; arr.col = 5;
    st.insert(arr);

    SymbolEntry* p = st.lookup("table");
    CHECK(p != nullptr,       "array 'table' found");
    CHECK(p->kind == SymbolKind::ARRAY, "kind == ARRAY");
    CHECK(p->type == SymType::TYPE_ARRAY, "type == TYPE_ARRAY");
    CHECK(p->array_start == 1,  "array_start == 1");
    CHECK(p->array_end   == 50, "array_end == 50");
}

// =============================================================================
static void test_remove() {
    std::cout << "\n[TEST] remove()\n";
    SymbolTable st;

    SymbolEntry e; e.name="r"; e.kind=SymbolKind::VARIABLE;
    e.type=SymType::TYPE_INTEGER; e.line=1; e.col=1;
    st.insert(e);

    CHECK(st.lookup("r") != nullptr, "'r' present before remove");

    bool ok = st.remove("r", 0);
    CHECK(ok, "remove('r', 0) returns true");
    CHECK(st.lookup("r") == nullptr, "'r' gone after remove");

    bool ok2 = st.remove("r", 0);
    CHECK(!ok2, "remove('r', 0) second time returns false (not found)");
}

// =============================================================================
static void test_hash_collisions() {
    std::cout << "\n[TEST] Many insertions (stress / collision handling)\n";
    SymbolTable st;

    // Insert 100 variables; at least a few will hash to the same bucket
    int insertOk = 0;
    for (int i = 0; i < 100; ++i) {
        SymbolEntry e;
        e.name  = "var" + std::to_string(i);
        e.kind  = SymbolKind::VARIABLE;
        e.type  = SymType::TYPE_INTEGER;
        e.line  = i + 1; e.col = 1;
        if (st.insert(e)) ++insertOk;
    }
    CHECK(insertOk == 100, "100 distinct variables inserted without error");

    // Look up a selection
    int lookupOk = 0;
    for (int i = 0; i < 100; i += 7) {
        if (st.lookup("var" + std::to_string(i))) ++lookupOk;
    }
    CHECK(lookupOk == 15, "lookup of var0,var7,...,var98 all succeed (15 checks)");
}

// =============================================================================
static void test_dump() {
    std::cout << "\n[TEST] dump() (visual inspection only)\n";
    SymbolTable st;

    SymbolEntry prog; prog.name="testprog"; prog.kind=SymbolKind::PROGRAM;
    prog.type=SymType::TYPE_VOID; prog.line=1; prog.col=9;
    st.insert(prog);

    SymbolEntry v1; v1.name="count"; v1.kind=SymbolKind::VARIABLE;
    v1.type=SymType::TYPE_INTEGER; v1.line=3; v1.col=5;
    st.insert(v1);

    SymbolEntry arr; arr.name="data"; arr.kind=SymbolKind::ARRAY;
    arr.type=SymType::TYPE_ARRAY; arr.array_start=1; arr.array_end=10;
    arr.line=4; arr.col=5;
    st.insert(arr);

    st.enterScope();
    SymbolEntry fn; fn.name="compute"; fn.kind=SymbolKind::FUNCTION;
    fn.type=SymType::TYPE_REAL; fn.param_count=3; fn.line=7; fn.col=10;
    st.insert(fn);

    SymbolEntry p1; p1.name="x"; p1.kind=SymbolKind::PARAMETER;
    p1.type=SymType::TYPE_REAL; p1.line=7; p1.col=18;
    st.insert(p1);

    st.dump();
    CHECK(true, "dump() executed without crash");
}

// =============================================================================
int main() {
    std::cout << "============================================\n";
    std::cout << "  Symbol Table Unit Tests\n";
    std::cout << "============================================\n";

    test_basic_insert_lookup();
    test_duplicate_in_same_scope();
    test_scoping_and_shadowing();
    test_multiple_scopes();
    test_array_entry();
    test_remove();
    test_hash_collisions();
    test_dump();

    std::cout << "\n============================================\n";
    std::cout << "  Results: " << passed << " passed, "
                               << failed << " failed\n";
    std::cout << "============================================\n";

    ErrorHandler::instance().printSummary();
    return failed > 0 ? 1 : 0;
}
