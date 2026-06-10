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
#include <limits>

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
// Usage (CLI mode)
// =============================================================================
static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --parser <rd|ll1|lr> [--verbose] <source_file>\n";
    std::cerr << "  --parser rd   : Recursive Descent parser\n";
    std::cerr << "  --parser ll1  : Predictive LL(1) parser\n";
    std::cerr << "  --parser lr   : LALR(1) LR parser\n";
    std::cerr << "  --verbose     : Enable extra diagnostic output\n";
    std::cerr << "  (no arguments): Launch interactive menu\n";
}

// =============================================================================
// runCompiler — shared compile pipeline
// =============================================================================
static int runCompiler() {
    printBanner(g_parserMode);

    CompilationStats stats;

    try {
        // 1. Count tokens
        {
            Lexer counter(g_sourceFile);
            Token t;
            do {
                t = counter.nextToken();
                ++stats.tokenCount;
            } while (t.type != TokenType::EOF_TOKEN);
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

        std::cout << (ok ? "Parse successful.\n" : "Parse failed.\n");

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

    ErrorHandler::instance().printSummary(g_sourceFile, &stats);
    return ErrorHandler::instance().hasErrors() ? 1 : 0;
}

// =============================================================================
// interactiveMenu — launched when no CLI args are given
// =============================================================================
static void interactiveMenu() {
    while (true) {
        // Reset error handler state between runs
        ErrorHandler::instance().reset();
        g_verbose = false;

        std::cout << "\n";
        std::cout << "╔═══════════════════════════════════════╗\n";
        std::cout << "║     Mini Pascal Compiler              ║\n";
        std::cout << "║     CS-471L — UET Lahore 2026         ║\n";
        std::cout << "╚═══════════════════════════════════════╝\n";
        std::cout << "\n";
        std::cout << "  Select parser:\n";
        std::cout << "    1)  Recursive Descent  (RD)\n";
        std::cout << "    2)  Predictive LL(1)\n";
        std::cout << "    3)  LR / SLR(1)\n";
        std::cout << "    0)  Exit\n";
        std::cout << "\n";
        std::cout << "  > ";

        int choice = -1;
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (choice == 0) {
            std::cout << "  Bye!\n\n";
            break;
        }

        if (choice < 1 || choice > 3) {
            std::cout << "  Invalid choice. Enter 1, 2, 3, or 0.\n";
            continue;
        }

        switch (choice) {
            case 1: g_parserMode = ParserMode::RD;  break;
            case 2: g_parserMode = ParserMode::LL1; break;
            case 3: g_parserMode = ParserMode::LR;  break;
        }

        // Ask for verbose
        std::cout << "\n  Enable verbose trace? (y/n): ";
        std::string yn;
        std::getline(std::cin, yn);
        g_verbose = (!yn.empty() && (yn[0] == 'y' || yn[0] == 'Y'));

        // Ask for source file
        std::cout << "\n  Enter source file path: ";
        std::getline(std::cin, g_sourceFile);

        // Trim whitespace
        while (!g_sourceFile.empty() && g_sourceFile.front() == ' ')
            g_sourceFile.erase(g_sourceFile.begin());
        while (!g_sourceFile.empty() && g_sourceFile.back() == ' ')
            g_sourceFile.pop_back();

        if (g_sourceFile.empty()) {
            std::cout << "  No file entered.\n";
            continue;
        }

        // Check file exists
        {
            std::ifstream f(g_sourceFile);
            if (!f.good()) {
                std::cout << "  Error: file '" << g_sourceFile << "' not found.\n";
                continue;
            }
        }

        std::cout << "\n";
        runCompiler();

        std::cout << "\n  Press Enter to continue...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char* argv[]) {

    // ---- no arguments → interactive menu ------------------------------------
    if (argc == 1) {
        interactiveMenu();
        return 0;
    }

    // ---- CLI mode -----------------------------------------------------------
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
            g_sourceFile = argv[i];
        }
    }

    if (g_sourceFile.empty()) {
        std::cerr << "Error: no source file specified.\n";
        printUsage(argv[0]);
        return 1;
    }

    return runCompiler();
}
