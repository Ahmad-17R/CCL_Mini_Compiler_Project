#pragma once
#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <string>
#include <vector>

// =============================================================================
// ErrorHandler — singleton; collects and reports all compiler diagnostics.
//
// Error categories (for the summary):
//   LEX   — lexical errors (illegal chars, malformed numbers, bad comments)
//   SYN   — syntactic errors (unexpected tokens, recovery actions)
//   SEM   — semantic errors (undeclared vars, type mismatches, duplicates)
//
// Output format (immediate, printed as each error occurs):
//   Error [line:col]: <message>
//   Warning [line:col]: <message>
//
// Final summary (tabular ASCII art, printed once at end of compilation):
//   MODULE 6 error table + ERROR SUMMARY + COMPILATION SUMMARY
// =============================================================================

enum class ErrKind { LEX, SYN, SEM, WARN };

struct ErrRecord {
    ErrKind     kind;
    int         line, col;
    std::string msg;
};

// Stats gathered by main() to populate the COMPILATION SUMMARY block.
struct CompilationStats {
    int  tokenCount    = 0;
    bool rdResult      = false;
    bool ll1Result     = false;
    bool lrResult      = false;
    int  symTableSize  = 0;
    bool rdRan         = false;   // was this parser actually invoked?
    bool ll1Ran        = false;
    bool lrRan         = false;
};

class ErrorHandler {
public:
    // Singleton access
    static ErrorHandler& instance();

    // Delete copy/move
    ErrorHandler(const ErrorHandler&)            = delete;
    ErrorHandler& operator=(const ErrorHandler&) = delete;

    // ---- categorised reporting ----------------------------------------------
    // Generic — category inferred as SYN if not specified
    void reportError  (int line, int col, const std::string& msg);
    void reportWarning(int line, int col, const std::string& msg);

    // Explicit category helpers
    void lexError (int line, int col, const std::string& msg);
    void synError (int line, int col, const std::string& msg);
    void semError (int line, int col, const std::string& msg);

    // ---- state ---------------------------------------------------------------
    int  errorCount()   const { return errorCount_;   }
    int  warningCount() const { return warningCount_; }
    bool hasErrors()    const { return errorCount_ > 0; }

    // ---- output --------------------------------------------------------------

    // Print full tabular summary (MODULE 6 table + ERROR SUMMARY).
    // Optionally pass stats for the COMPILATION SUMMARY block.
    // Called once at the end of main().
    void printSummary(const std::string& sourceFile = "",
                      const CompilationStats* stats = nullptr) const;

    // Abort compilation if MAX_ERRORS has been exceeded.
    void checkLimit() const;

    // Reset (useful between test runs)
    void reset();

private:
    ErrorHandler() : errorCount_(0), warningCount_(0) {}

    void emit(ErrKind kind, int line, int col, const std::string& msg);

    int                     errorCount_;
    int                     warningCount_;
    std::vector<ErrRecord>  records_;   // all messages, in arrival order
};

#endif // ERROR_HANDLER_H
