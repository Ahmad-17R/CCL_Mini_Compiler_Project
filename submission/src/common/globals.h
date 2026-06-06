#pragma once
#ifndef GLOBALS_H
#define GLOBALS_H

#include <string>

// =============================================================================
// Compiler-wide constants
// =============================================================================

// Buffer size for the lexer's double-buffer scheme (each half)
constexpr int LEXER_BUFFER_HALF = 4096;

// Maximum identifier / lexeme length
constexpr int MAX_LEXEME_LEN = 256;

// Maximum number of errors before the compiler aborts
constexpr int MAX_ERRORS = 25;

// =============================================================================
// Parser selection enum — set once by main, read by the pipeline
// =============================================================================
enum class ParserMode {
    RD,   // Recursive Descent
    LL1,  // Predictive LL(1) table-driven
    LR    // LALR(1) shift-reduce
};

// =============================================================================
// Global compiler state (defined in main.cpp, declared extern here)
// =============================================================================
extern ParserMode  g_parserMode;   // which parser is active
extern std::string g_sourceFile;   // path to the input source file
extern bool        g_verbose;      // enable extra diagnostic output
extern int         g_errorCount;   // running error tally

// =============================================================================
// Utility: pretty-print the parser mode name
// =============================================================================
inline std::string parserModeToString(ParserMode m) {
    switch (m) {
        case ParserMode::RD:  return "Recursive Descent (RD)";
        case ParserMode::LL1: return "Predictive LL(1)";
        case ParserMode::LR:  return "LALR(1) LR";
        default:              return "Unknown";
    }
}

#endif // GLOBALS_H
