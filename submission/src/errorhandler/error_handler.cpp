#include "error_handler.h"
#include "../common/globals.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

// =============================================================================
// Singleton
// =============================================================================
ErrorHandler& ErrorHandler::instance() {
    static ErrorHandler inst;
    return inst;
}

// =============================================================================
// reset — clear all state (useful between runs in tests)
// =============================================================================
void ErrorHandler::reset() {
    errorCount_   = 0;
    warningCount_ = 0;
    records_.clear();
}

// =============================================================================
// Internal emit — stores record and prints immediately to stderr
// =============================================================================
void ErrorHandler::emit(ErrKind kind, int line, int col, const std::string& msg) {
    records_.push_back({kind, line, col, msg});

    // Immediate inline format: "Error [line:col]: message"
    if (kind == ErrKind::WARN)
        std::cerr << "Warning [" << line << ":" << col << "]: " << msg << "\n";
    else
        std::cerr << "Error [" << line << ":" << col << "]: " << msg << "\n";
}

// =============================================================================
// Public reporting
// =============================================================================
void ErrorHandler::reportError(int line, int col, const std::string& msg) {
    ++errorCount_;
    emit(ErrKind::SYN, line, col, msg);
    checkLimit();
}

void ErrorHandler::reportWarning(int line, int col, const std::string& msg) {
    ++warningCount_;
    emit(ErrKind::WARN, line, col, msg);
}

void ErrorHandler::lexError(int line, int col, const std::string& msg) {
    ++errorCount_;
    emit(ErrKind::LEX, line, col, msg);
    checkLimit();
}

void ErrorHandler::synError(int line, int col, const std::string& msg) {
    ++errorCount_;
    emit(ErrKind::SYN, line, col, msg);
    checkLimit();
}

void ErrorHandler::semError(int line, int col, const std::string& msg) {
    ++errorCount_;
    emit(ErrKind::SEM, line, col, msg);
    checkLimit();
}

// =============================================================================
// utf8DisplayWidth — approximate display width of a UTF-8 string
// (counts bytes that are not UTF-8 continuation bytes, i.e. not 10xxxxxx)
// =============================================================================
static int utf8DisplayWidth(const std::string& s) {
    int w = 0;
    for (unsigned char c : s) {
        // Count only bytes that start a code-point (not continuation bytes)
        if ((c & 0xC0) != 0x80) ++w;
    }
    return w;
}

// =============================================================================
// printSummary — tabular ASCII-art output (matches sample project format)
// =============================================================================
void ErrorHandler::printSummary(const std::string& sourceFile,
                                const CompilationStats* stats) const {
    // -------------------------------------------------------------------------
    // Helper: column widths for the error table
    //   #  (5) | Type (11) | Line (6) | Col (5) | Message (dynamic, min 21)
    // -------------------------------------------------------------------------
    static const int W_NUM  = 5;
    static const int W_TYPE = 11;
    static const int W_LINE = 6;
    static const int W_COL  = 5;
    static const int W_MSG_MIN = 21;   // "No errors detected" is 18 chars

    // Find the longest message for dynamic column width (use display width)
    int msgWidth = W_MSG_MIN;
    for (auto& r : records_) {
        msgWidth = std::max(msgWidth, utf8DisplayWidth(r.msg) + 2);
    }
    // Also account for the header label
    msgWidth = std::max(msgWidth, (int)std::string("Message").size() + 2);

    // Total table width = 1 (|) + W_NUM+2 + 1 + W_TYPE+2 + 1 + W_LINE+2 + 1 + W_COL+2 + 1 + msgWidth + 1
    int totalW = 1 + (W_NUM+2) + 1 + (W_TYPE+2) + 1 + (W_LINE+2) + 1 + (W_COL+2) + 1 + msgWidth + 1;

    auto hline = [&](std::ostream& os) {
        os << "+";
        os << std::string(W_NUM+2, '-') << "+";
        os << std::string(W_TYPE+2, '-') << "+";
        os << std::string(W_LINE+2, '-') << "+";
        os << std::string(W_COL+2, '-') << "+";
        os << std::string(msgWidth, '-') << "+";
        os << "\n";
    };

    auto row = [&](std::ostream& os,
                   const std::string& num,
                   const std::string& type,
                   const std::string& line,
                   const std::string& col,
                   const std::string& msg) {
        // For the message column, compute extra byte-vs-display padding
        int dispW  = utf8DisplayWidth(msg);
        int byteW  = (int)msg.size();
        int extra  = byteW - dispW;  // extra bytes due to multi-byte chars
        // setw counts bytes; to achieve visual width of msgWidth-1 we need
        // to give setw an effective width that accounts for the extra bytes.
        int effW = (msgWidth - 1) + extra;
        os << "| " << std::left << std::setw(W_NUM)  << num  << " "
           << "| " << std::left << std::setw(W_TYPE) << type << " "
           << "| " << std::left << std::setw(W_LINE) << line << " "
           << "| " << std::left << std::setw(W_COL)  << col  << " "
           << "| " << std::left << std::setw(effW)   << msg  << "|\n";
    };

    std::ostream& os = std::cerr;

    // -------------------------------------------------------------------------
    // MODULE 6 banner — width derived from the error table width
    // -------------------------------------------------------------------------
    // Build a fixed-width banner that exactly spans totalW characters
    {
        int innerW = totalW - 2;  // characters between the two outer | marks
        std::string title = "  MODULE 6 - ERROR HANDLER";
        int pad = innerW - (int)title.size();
        os << "|" << title << std::string(std::max(pad, 0), ' ') << "|\n";
        os << "+" << std::string(innerW, '=') << "+\n";
    }

    // -------------------------------------------------------------------------
    // Error table
    // -------------------------------------------------------------------------
    hline(os);
    row(os, "#", "Type", "Line", "Col", "Message");
    hline(os);

    if (records_.empty()) {
        row(os, "", "", "", "", "No errors detected");
    } else {
        int num = 0;
        for (auto& r : records_) {
            ++num;
            std::string typeStr;
            switch (r.kind) {
                case ErrKind::LEX:  typeStr = "LEXICAL";   break;
                case ErrKind::SYN:  typeStr = "SYNTACTIC"; break;
                case ErrKind::SEM:  typeStr = "SEMANTIC";  break;
                case ErrKind::WARN: typeStr = "WARNING";   break;
            }
            row(os,
                std::to_string(num),
                typeStr,
                std::to_string(r.line),
                std::to_string(r.col),
                r.msg);
        }
    }
    hline(os);

    // -------------------------------------------------------------------------
    // ERROR SUMMARY box
    // -------------------------------------------------------------------------
    // Count by category
    int nLex = 0, nSyn = 0, nSem = 0;
    for (auto& r : records_) {
        if      (r.kind == ErrKind::LEX)  ++nLex;
        else if (r.kind == ErrKind::SYN)  ++nSyn;
        else if (r.kind == ErrKind::SEM)  ++nSem;
    }

    // Fixed-width summary box (38 chars wide, matching sample project)
    static const int SBOX = 38;   // inner width (between the two |'s)
    auto sline = [&](std::ostream& o) {
        o << "+" << std::string(SBOX, '-') << "+\n";
    };
    auto srow = [&](std::ostream& o, const std::string& label, int val) {
        std::ostringstream tmp;
        tmp << "|   " << std::left << std::setw(22) << label
            << ": " << std::right << std::setw(7) << val << " |\n";
        o << tmp.str();
    };

    os << "\n";
    sline(os);
    os << "| " << std::left << std::setw(SBOX-1) << "ERROR SUMMARY" << "|\n";
    sline(os);
    srow(os, "Total errors",   errorCount_);
    srow(os, "Lexical",        nLex);
    srow(os, "Syntactic",      nSyn);
    srow(os, "Semantic",       nSem);
    sline(os);

    // -------------------------------------------------------------------------
    // COMPILATION SUMMARY (only when stats are provided)
    // -------------------------------------------------------------------------
    if (stats) {
        // Derive the short filename for the title (strip leading path components)
        std::string fname = sourceFile;
        auto slash = fname.rfind('/');
        if (slash != std::string::npos) fname = fname.substr(slash + 1);
        // Keep one parent dir if available (e.g. "test/sample.pas")
        // We already have the full sourceFile string so just use it as-is for
        // the label (matches the sample project's "test/valid1.pas" style).
        std::string label = sourceFile.empty() ? fname : sourceFile;

        std::string compTitle = "| COMPILATION SUMMARY - " + label + "   ";
        int cboxW = std::max(43, (int)compTitle.size() + 1);
        // Pad title to cboxW
        int tpad = cboxW - (int)compTitle.size() - 1;
        compTitle = compTitle + std::string(std::max(tpad, 0), ' ') + "|";

        auto cline = [&](std::ostream& o) {
            o << "+" << std::string(cboxW - 2, '-') << "+\n";
        };

        auto crow = [&](std::ostream& o, const std::string& lbl, const std::string& val) {
            std::string line = "| " + lbl + " " + val;
            int rpad = cboxW - (int)line.size() - 2;
            o << line << std::string(std::max(rpad, 0), ' ') << " |\n";
        };

        os << "\n";
        cline(os);
        os << compTitle << "\n";
        cline(os);
        crow(os, "Tokens      :", std::to_string(stats->tokenCount));
        if (stats->rdRan)
            crow(os, "RD Parser   :", stats->rdResult  ? "ACCEPT" : "REJECT");
        if (stats->ll1Ran)
            crow(os, "LL1 Parser  :", stats->ll1Result ? "ACCEPT" : "REJECT");
        if (stats->lrRan)
            crow(os, "LR Parser   :", stats->lrResult  ? "ACCEPT" : "REJECT");
        crow(os, "Symbol Table:", std::to_string(stats->symTableSize) + " entries");
        cline(os);
    }

    os << "\n";
}

// =============================================================================
// checkLimit — hard stop after MAX_ERRORS
// =============================================================================
void ErrorHandler::checkLimit() const {
    if (errorCount_ >= MAX_ERRORS) {
        std::cerr << "\nError [0:0]: Too many errors ("
                  << MAX_ERRORS << "). Aborting compilation.\n";
        throw std::runtime_error("too many errors");
    }
}
