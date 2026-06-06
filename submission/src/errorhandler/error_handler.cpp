#include "error_handler.h"
#include "../common/globals.h"

#include <iostream>
#include <stdexcept>

// =============================================================================
// Singleton
// =============================================================================
ErrorHandler& ErrorHandler::instance() {
    static ErrorHandler inst;
    return inst;
}

// =============================================================================
// reportError
// =============================================================================
void ErrorHandler::reportError(int line, int col, const std::string& msg) {
    ++errorCount_;
    std::cerr << "[ERROR] " << g_sourceFile
              << ":" << line << ":" << col
              << "  " << msg << "\n";
    checkLimit();
}

// =============================================================================
// reportWarning
// =============================================================================
void ErrorHandler::reportWarning(int line, int col, const std::string& msg) {
    ++warningCount_;
    std::cerr << "[WARN]  " << g_sourceFile
              << ":" << line << ":" << col
              << "  " << msg << "\n";
}

// =============================================================================
// printSummary
// =============================================================================
void ErrorHandler::printSummary() const {
    std::cerr << "\nCompilation finished: "
              << errorCount_   << " error(s), "
              << warningCount_ << " warning(s).\n";
}

// =============================================================================
// checkLimit — hard stop after MAX_ERRORS
// =============================================================================
void ErrorHandler::checkLimit() const {
    if (errorCount_ >= MAX_ERRORS) {
        std::cerr << "[FATAL] Too many errors (" << MAX_ERRORS
                  << "). Aborting compilation.\n";
        throw std::runtime_error("too many errors");
    }
}
