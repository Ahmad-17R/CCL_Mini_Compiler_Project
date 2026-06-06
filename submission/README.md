# Mini Pascal Compiler — CS-471L Compiler Construction Lab
**UET Lahore, Spring 2026**

---

## Project Overview

A complete mini compiler for a Pascal subset, implemented in C++17 across 6 integrated modules:

| Module | Description |
|--------|-------------|
| **Lexer** | Double-buffered tokenizer; recognizes all terminals of the Pascal subset |
| **Recursive Descent Parser** | Top-down, one function per non-terminal |
| **Predictive LL(1) Parser** | Table-driven, FIRST/FOLLOW based |
| **LR Parser** | Bottom-up SLR(1) shift-reduce with auto-generated action/goto tables |
| **Symbol Table** | Hash-based, scoped (scope stack), tracks all identifiers |
| **Error Handler** | Panic-mode + phrase-level recovery; categorized tabular summary |

---

## Pipeline

```
Source File (.pas)
      │
      ▼
   Lexer  ──────────────────────────────────────────► Token Stream
      │                                                     │
      ▼                                                     ▼
  Parser  (RD | LL1 | LR)  ◄──────────────────  Symbol Table
      │                                                     │
      └──────────────────────────────► Error Handler ◄──────┘
                                             │
                                             ▼
                                    Tabular Error Report
                                    + Compilation Summary
```

---

## Build

```bash
make          # builds ./minicompiler
make clean    # removes all .o files and the binary
```

Requires: `g++` with C++17 support (`-std=c++17`).

---

## Usage

```bash
./minicompiler --parser <rd|ll1|lr> [--verbose] <source_file>
```

| Flag | Description |
|------|-------------|
| `--parser rd` | Recursive Descent parser |
| `--parser ll1` | Predictive LL(1) parser |
| `--parser lr` | LALR(1) LR parser |
| `--verbose` | Print parse trace, FIRST/FOLLOW sets, parsing tables, symbol table dump |

Exit code `0` = no errors, `1` = one or more errors detected.

---

## Test Programs

All test files are in `test/`.

| File | Purpose | Expected Result |
|------|---------|----------------|
| `sample.pas` | Basic smoke test — variables, arithmetic, if/while | ACCEPT (all 3 parsers) |
| `rd_valid.pas` | Full-featured — arrays, functions, procedures, nested loops, logical ops | ACCEPT (all 3 parsers) |
| `rd_error.pas` | Missing expression after `:=` | REJECT — 1 syntactic error |
| `multi_error.pas` | Missing expression + undeclared identifier | REJECT — 1 syntactic + 1 semantic error |
| `edge_cases.pas` | Lexer stress — dotdot, real exponents, illegal character `@` | REJECT — 1 lexical + 1 syntactic error |

---

## Running All Sample Programs End-to-End

Run every test file through every parser in one pass:

```bash
for f in test/sample.pas test/rd_valid.pas test/rd_error.pas test/multi_error.pas test/edge_cases.pas; do
    for p in rd ll1 lr; do
        echo "=== $p : $f ==="
        ./minicompiler --parser $p "$f" 2>&1
        echo ""
    done
done
```

Or use the built-in smoke test (runs `sample.pas` through all three parsers):

```bash
make test
```

### Expected Results Summary

| File | RD | LL1 | LR |
|------|----|-----|----|
| `sample.pas` | ACCEPT | ACCEPT | ACCEPT |
| `rd_valid.pas` | ACCEPT | ACCEPT | ACCEPT |
| `rd_error.pas` | REJECT | REJECT | REJECT |
| `multi_error.pas` | REJECT | REJECT | REJECT |
| `edge_cases.pas` | REJECT | REJECT | REJECT |

All three parsers must agree on every input — same accept/reject result.

---

## Demo Guide — Member Responsibilities

### Member 1 — Lexer, Symbol Table, Recursive Descent Parser

**Modules to explain:** `src/lexer/`, `src/symtable/`, `src/parser_rd/`

**Demo script:**

1. **Lexer** — show token stream on `sample.pas`:
   ```bash
   ./minicompiler --parser rd --verbose test/sample.pas 2>&1 | head -40
   ```
   Explain: double-buffer design, keyword vs identifier recognition, number literal scanning (integer/real/exponent), comment skipping, line/col tracking on every token.

2. **Symbol Table** — show scoped entries on `rd_valid.pas`:
   ```bash
   ./minicompiler --parser rd --verbose test/rd_valid.pas 2>&1 | grep -A 100 "SYMBOL TABLE"
   ```
   Explain: djb2 hash function, chaining collision resolution, scope stack (`enterScope`/`exitScope`), duplicate declaration detection.

3. **Recursive Descent Parser** — show trace on `sample.pas` then error on `rd_error.pas`:
   ```bash
   ./minicompiler --parser rd --verbose test/sample.pas 2>&1 | grep "\[RD\]"
   ./minicompiler --parser rd test/rd_error.pas 2>&1
   ```
   Explain: one function per non-terminal, lookahead-driven prediction, how `match()` calls the error handler, the `ParseException` catch in `parse()`, semantic checks on assignment (type mismatch) and factor (undeclared identifier).

**Phase 7 errors to highlight:**

```bash
./minicompiler --parser rd test/multi_error.pas 2>&1
```
Point to the MODULE 6 table: row 1 is syntactic (missing expression), row 2 is semantic (undeclared `z`). Show ERROR SUMMARY counts by category and the COMPILATION SUMMARY token/symtable counts.

---

### Member 2 — LL(1) Parser, LR Parser, Error Handler

**Modules to explain:** `src/parser_ll1/`, `src/parser_lr/`, `src/errorhandler/`

**Demo script:**

1. **LL(1) Parser** — show FIRST/FOLLOW sets and parsing table, then run on valid + invalid input:
   ```bash
   ./minicompiler --parser ll1 --verbose test/sample.pas 2>&1 | head -80
   ./minicompiler --parser ll1 test/multi_error.pas 2>&1
   ```
   Explain: hardcoded FIRST/FOLLOW sets, 2D parsing table (`table[NonTerminal][Terminal]`), explicit stack-based parse loop (no recursion), FOLLOW-set panic recovery (`panicRecover`): skip tokens until a sync token in FOLLOW(current NT) is found, then resume.

2. **LR Parser** — show action/goto tables and step-by-step trace, then run on all inputs:
   ```bash
   ./minicompiler --parser lr --verbose test/sample.pas 2>&1 | head -60
   ./minicompiler --parser lr test/rd_error.pas 2>&1
   ```
   Explain: automatic SLR(1) table construction from the grammar (LR(0) item sets → canonical collection → ACTION/GOTO), shift-reduce loop with two explicit stacks (state stack + symbol stack), shift/reduce conflict resolution (shift wins for dangling-else), panic-mode recovery using sync tokens (`;`, `end`, `begin`, `.`, `$`), `onReduce()` semantic hooks for undeclared variable checks and array subscript validation.

3. **Error Handler** — show the full tabular output:
   ```bash
   ./minicompiler --parser lr test/multi_error.pas 2>&1
   ./minicompiler --parser rd test/edge_cases.pas 2>&1
   ```
   Explain: singleton `ErrorHandler`, three categories (`lexError`, `synError`, `semError`), immediate `Error [line:col]: message` emission to stderr, `checkLimit()` hard stop at 25 errors, `printSummary()` output: MODULE 6 banner → error table (dynamic column width) → ERROR SUMMARY box (per-category counts) → COMPILATION SUMMARY box (token count, parser result, symbol table size).

---

## Project Structure

```
submission/
├── Makefile
├── README.md
├── src/
│   ├── main.cpp                     ← pipeline wiring, CLI flags, stats collection
│   ├── common/
│   │   ├── globals.h                ← shared constants, ParserMode enum
│   │   └── token.h                  ← Token struct, TokenType enum
│   ├── lexer/
│   │   ├── lexer.h
│   │   └── lexer.cpp                ← double-buffer, all token recognition
│   ├── symtable/
│   │   ├── symbol_table.h
│   │   └── symbol_table.cpp         ← hash table, scope stack, insert/lookup
│   ├── parser_rd/
│   │   ├── parser_rd.h
│   │   └── parser_rd.cpp            ← recursive descent, semantic checks
│   ├── parser_ll1/
│   │   ├── parser_ll1.h
│   │   └── parser_ll1.cpp           ← LL(1) table-driven, panic recovery
│   ├── parser_lr/
│   │   ├── parser_lr.h
│   │   └── parser_lr.cpp            ← SLR(1) auto-tables, shift-reduce loop
│   └── errorhandler/
│       ├── error_handler.h
│       └── error_handler.cpp        ← singleton, categorized errors, tabular summary
└── test/
    ├── sample.pas                   ← basic smoke test
    ├── rd_valid.pas                 ← full-featured valid program
    ├── rd_error.pas                 ← syntactic error
    ├── multi_error.pas              ← multiple errors (syntactic + semantic)
    └── edge_cases.pas               ← lexer edge cases + illegal character
```

---

## Error Output Format

Every error is emitted immediately to `stderr` as:
```
Error [line:col]: <message>
Warning [line:col]: <message>
```

At end of compilation, a full tabular report is printed:

```
|  MODULE 6 - ERROR HANDLER         |
+====================================+
+-----+-----------+------+-----+---------------------+
| #   | Type      | Line | Col | Message             |
+-----+-----------+------+-----+---------------------+
|   1 | SYNTACTIC |    8 |  10 | ...                 |
|   2 | SEMANTIC  |   10 |   5 | ...                 |
+-----+-----------+------+-----+---------------------+

+------------------------------------+
| ERROR SUMMARY                      |
+------------------------------------+
|   Total errors         :        2 |
|   Lexical              :        0 |
|   Syntactic            :        1 |
|   Semantic             :        1 |
+------------------------------------+

+------------------------------------------+
| COMPILATION SUMMARY - test/sample.pas    |
+------------------------------------------+
| Tokens      : 45                         |
| RD Parser   : ACCEPT                     |
| Symbol Table: 4 entries                  |
+------------------------------------------+
```
