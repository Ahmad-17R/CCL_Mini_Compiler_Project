#include "common/globals.h"
#include "common/token.h"
#include "lexer/lexer.h"
#include "symtable/symbol_table.h"
#include "parser_rd/parser_rd.h"
#include "parser_ll1/parser_ll1.h"
#include "parser_lr/parser_lr.h"
#include "errorhandler/error_handler.h"

#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <fstream>

// =============================================================================
// Global state definitions (declared extern in globals.h)
// =============================================================================
ParserMode  g_parserMode  = ParserMode::RD;
std::string g_sourceFile;
bool        g_verbose     = false;
int         g_errorCount  = 0;

// =============================================================================
// Banner
// =============================================================================
static void printBanner(ParserMode mode) {
    std::cout << "=========================================\n";
    std::cout << "  Mini Pascal Compiler\n";
    std::cout << "  Parser : " << parserModeToString(mode) << "\n";
    std::cout << "  Source : " << g_sourceFile << "\n";
    std::cout << "=========================================\n\n";
}

// =============================================================================
// Usage
// =============================================================================
static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --parser <rd|ll1|lr> [--verbose] <source_file>\n";
    std::cerr << "  --parser rd   : Recursive Descent parser\n";
    std::cerr << "  --parser ll1  : Predictive LL(1) parser\n";
    std::cerr << "  --parser lr   : LALR(1) LR parser\n";
    std::cerr << "  --verbose     : Enable extra diagnostic output\n";
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char* argv[]) {
    // ---- argument parsing ---------------------------------------------------
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--parser") == 0) {
            if (++i >= argc) {
                std::cerr << "Error: --parser requires an argument (rd | ll1 | lr)\n";
                return 1;
            }
            std::string mode(argv[i]);
            if      (mode == "rd")  g_parserMode = ParserMode::RD;
            else if (mode == "ll1") g_parserMode = ParserMode::LL1;
            else if (mode == "lr")  g_parserMode = ParserMode::LR;
            else {
                std::cerr << "Error: unknown parser '" << mode
                          << "'. Choose rd, ll1, or lr.\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            g_verbose = true;
        } else {
            // Treat as the source file
            g_sourceFile = argv[i];
        }
    }

    if (g_sourceFile.empty()) {
        std::cerr << "Error: no source file specified.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ---- startup banner -----------------------------------------------------
    printBanner(g_parserMode);

    // ---- compile pipeline ---------------------------------------------------
    CompilationStats stats;

    try {
        // 1. Count tokens for the compilation summary (separate lexer pass)
        {
            Lexer counter(g_sourceFile);
            Token t;
            do {
                t = counter.nextToken();
                ++stats.tokenCount;
            } while (t.type != TokenType::EOF_TOKEN);
            // Subtract the EOF sentinel so count matches real tokens only
            if (stats.tokenCount > 0) --stats.tokenCount;
        }

        // 2. Lexer for parsing
        Lexer lexer(g_sourceFile);

        // 3. Symbol table
        SymbolTable symTable;

        // 4. Parse
        bool ok = false;
        switch (g_parserMode) {
            case ParserMode::RD: {
                ParserRD parser(lexer, symTable);
                ok = parser.parse();
                stats.rdRan    = true;
                stats.rdResult = ok;
                break;
            }
            case ParserMode::LL1: {
                ParserLL1 parser(lexer, symTable);
                if (g_verbose) {
                    parser.printFirstFollowSets();
                    parser.printParsingTable();
                }
                ok = parser.parse();
                stats.ll1Ran    = true;
                stats.ll1Result = ok;
                break;
            }
            case ParserMode::LR: {
                ParserLR parser(lexer, symTable);
                if (g_verbose) {
                    parser.printActionTable();
                    parser.printGotoTable();
                }
                ok = parser.parse();
                stats.lrRan    = true;
                stats.lrResult = ok;
                break;
            }
        }
        if (ok)
            std::cout << "Parse successful.\n";
        else
            std::cout << "Parse failed.\n";

        // 5. Symbol table stats + optional dump
        stats.symTableSize = symTable.size();
        if (g_verbose) symTable.dump();

    } catch (const std::runtime_error& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        ErrorHandler::instance().printSummary(g_sourceFile, &stats);
        return 1;
    } catch (...) {
        std::cerr << "[FATAL] Unknown exception during compilation.\n";
        return 1;
    }

    // ---- final summary -------------------------------------------------------
    ErrorHandler::instance().printSummary(g_sourceFile, &stats);

    return ErrorHandler::instance().hasErrors() ? 1 : 0;
}
