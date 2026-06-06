#pragma once
#ifndef LEXER_H
#define LEXER_H

#include "../common/token.h"
#include <string>
#include <fstream>

// =============================================================================
// Lexer — double-buffered, on-demand tokenizer for the Pascal subset.
//
// Design
// ------
//  • Two 4096-byte halves (buf_[0] and buf_[1]).  Each half has an extra
//    sentinel byte set to '\0' so the inner loop can detect end-of-half
//    without an explicit bounds check on every character.
//  • `forward_` is the read cursor inside the active half.
//  • When the cursor hits the sentinel the other half is reloaded from disk
//    and becomes active.
//  • A one-token look-ahead (hasPeeked_ / peekedToken_) supports peekToken()
//    without re-scanning.
//
// Position tracking
// -----------------
//  • `line_` and `col_` always reflect the position of the *last character
//    returned by nextChar()*.
//  • `tokLine_` / `tokCol_` are latched at the start of each token.
//  • retract() adjusts both counters correctly using a saved "previous col"
//    value so that col never goes negative after a newline.
// =============================================================================

class Lexer {
public:
    explicit Lexer(const std::string& filename);
    ~Lexer();

    // Return the next token (consumes it).
    Token nextToken();

    // Peek at the next token without consuming it.
    Token peekToken();

    // Alias kept for backward compatibility with parser_rd skeleton.
    Token peek() { return peekToken(); }

    // Convenience: run through the entire file and print every token to stdout.
    // Format per line:  [line:col]  TYPE  "lexeme"
    // Opens a *fresh* Lexer internally so the caller's state is untouched.
    static void printTokenStream(const std::string& filename);

    // Current source position (of the last character consumed by nextChar).
    int currentLine() const { return line_; }
    int currentCol()  const { return col_;  }

private:
    // ---- double buffer -------------------------------------------------------
    static constexpr int BUF_SIZE = 4096;
    char buf_[2][BUF_SIZE + 1]; // two halves; [n] = '\0' sentinel
    int  activeBuf_;            // which half is current (0 or 1)
    int  forward_;              // cursor within the active half
    bool srcEof_;               // true once the file has been fully read

    std::ifstream src_;

    // ---- position tracking --------------------------------------------------
    int line_;      // line of the last char returned by nextChar()
    int col_;       // col  of the last char returned by nextChar()
    int prevCol_;   // col  just *before* the last nextChar() call (for retract)

    // ---- one-token look-ahead -----------------------------------------------
    bool  hasPeeked_;
    Token peekedToken_;

    // ---- internal helpers ---------------------------------------------------
    void  loadBuffer(int half);
    char  nextChar();
    void  retract();

    Token scanToken();
    Token scanIdentifierOrKeyword(char first, int startLine, int startCol);
    Token scanNumber(char first, int startLine, int startCol);
    void  skipComment(int startLine, int startCol);
    void  skipWhitespace();
};

#endif // LEXER_H
