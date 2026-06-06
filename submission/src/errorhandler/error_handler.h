#pragma once
#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <string>
#include <vector>

// =============================================================================
// ErrorHandler — singleton; collects and reports compiler errors/warnings.
//
// Supports:
//   - Panic-mode recovery (caller skips tokens until a synchronising set)
//   - Phrase-level recovery (described in Phase 6 implementation)
//   - Every message carries line + column
// =============================================================================
class ErrorHandler {
public:
    // Singleton access
    static ErrorHandler& instance();

    // Delete copy/move
    ErrorHandler(const ErrorHandler&)            = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;

    // ---- reporting -----------------------------------------------------------
    void reportError  (int line, int col, const std::string& msg);
    void reportWarning(int line, int col, const std::string& msg);

    // ---- state ---------------------------------------------------------------
    int  errorCount()   const { return errorCount_;   }
    int  warningCount() const { return warningCount_; }
    bool hasErrors()    const { return errorCount_ > 0; }

    // Print summary line
    void printSummary() const;

    // Abort if too many errors have accumulated
    void checkLimit() const;

private:
    ErrorHandler() : errorCount_(0), warningCount_(0) {}

    int errorCount_;
    int warningCount_;
};

#endif // ERROR_HANDLER_H
