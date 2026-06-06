// =============================================================================
// lex_driver.cpp — standalone driver that prints the token stream for any file
// Usage:  ./lex_driver <source.pas>
// =============================================================================
#include "../src/common/globals.h"
#include "../src/common/token.h"
#include "../src/lexer/lexer.h"
#include "../src/errorhandler/error_handler.h"

#include <iostream>
#include <string>

// Define the globals that error_handler.cpp references
ParserMode  g_parserMode = ParserMode::RD;
std::string g_sourceFile;
bool        g_verbose    = false;
int         g_errorCount = 0;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source.pas>\n";
        return 1;
    }
    g_sourceFile = argv[1];

    try {
        Lexer::printTokenStream(g_sourceFile);
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    ErrorHandler::instance().printSummary();
    return ErrorHandler::instance().hasErrors() ? 1 : 0;
}
