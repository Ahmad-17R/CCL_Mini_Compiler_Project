#include "lexer.h"
#include "../common/globals.h"
#include "../errorhandler/error_handler.h"

#include <cctype>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>

// =============================================================================
// Static keyword table (lower-cased keys; Pascal is case-insensitive)
// =============================================================================
static const std::unordered_map<std::string, TokenType> KEYWORDS = {
    {"program",   TokenType::KW_PROGRAM},
    {"var",       TokenType::KW_VAR},
    {"array",     TokenType::KW_ARRAY},
    {"of",        TokenType::KW_OF},
    {"integer",   TokenType::KW_INTEGER},
    {"real",      TokenType::KW_REAL},
    {"function",  TokenType::KW_FUNCTION},
    {"procedure", TokenType::KW_PROCEDURE},
    {"begin",     TokenType::KW_BEGIN},
    {"end",       TokenType::KW_END},
    {"if",        TokenType::KW_IF},
    {"then",      TokenType::KW_THEN},
    {"else",      TokenType::KW_ELSE},
    {"while",     TokenType::KW_WHILE},
    {"do",        TokenType::KW_DO},
    {"not",       TokenType::KW_NOT},
    {"div",       TokenType::KW_DIV},
    {"mod",       TokenType::KW_MOD},
    {"and",       TokenType::KW_AND},
    {"or",        TokenType::KW_OR},
};

// =============================================================================
// Constructor
// =============================================================================
Lexer::Lexer(const std::string& filename)
    : activeBuf_(0), forward_(0), srcEof_(false),
      line_(1), col_(0), prevCol_(0),
      hasPeeked_(false), peekedToken_()
{
    src_.open(filename, std::ios::binary);
    if (!src_.is_open())
        throw std::runtime_error("Lexer: cannot open source file '" + filename + "'");

    // Install sentinels
    buf_[0][BUF_SIZE] = '\0';
    buf_[1][BUF_SIZE] = '\0';

    // Prime the first half
    loadBuffer(0);
}

// =============================================================================
// Destructor
// =============================================================================
Lexer::~Lexer() {
    if (src_.is_open()) src_.close();
}

// =============================================================================
// Public API
// =============================================================================
Token Lexer::nextToken() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peekedToken_;
    }
    return scanToken();
}

Token Lexer::peekToken() {
    if (!hasPeeked_) {
        peekedToken_ = scanToken();
        hasPeeked_   = true;
    }
    return peekedToken_;
}

// =============================================================================
// printTokenStream — static helper, opens its own Lexer instance
// =============================================================================
void Lexer::printTokenStream(const std::string& filename) {
    Lexer lex(filename);
    std::cout << "Token stream for: " << filename << "\n";
    std::cout << std::string(60, '-') << "\n";
    // Column widths for pretty alignment
    // [line:col]   TYPE_STRING   "lexeme"
    Token tok;
    do {
        tok = lex.nextToken();
        std::string pos = "[" + std::to_string(tok.line) + ":"
                              + std::to_string(tok.col)  + "]";
        std::cout << std::left
                  << std::setw(12) << pos
                  << std::setw(22) << tokenTypeToString(tok.type)
                  << "\"" << tok.lexeme << "\"\n";
    } while (tok.type != TokenType::EOF_TOKEN);
    std::cout << std::string(60, '-') << "\n";
}

// =============================================================================
// Double-buffer management
// =============================================================================
void Lexer::loadBuffer(int half) {
    src_.read(buf_[half], BUF_SIZE);
    std::streamsize n = src_.gcount();
    buf_[half][n] = '\0'; // sentinel
    if (n == 0) srcEof_ = true;
}

// nextChar() — advance forward_ by one, handle buffer switching and '\n'
// Returns '\0' on true EOF.
char Lexer::nextChar() {
    char c = buf_[activeBuf_][forward_];

    if (c == '\0') {
        // Hit sentinel — either real EOF or end-of-half
        if (srcEof_) return '\0';
        // Switch halves and reload the one we just left
        int newHalf  = activeBuf_ ^ 1;
        loadBuffer(newHalf); // reload the *other* half while we still have this one
        activeBuf_   = newHalf;
        forward_     = 0;
        c = buf_[activeBuf_][forward_];
        if (c == '\0') return '\0'; // truly empty read
    }

    ++forward_;
    prevCol_ = col_;          // save so retract() can restore it
    if (c == '\n') { ++line_; col_ = 0; }
    else           { ++col_;            }
    return c;
}

// retract() — undo the last nextChar() call (at most one retract at a time)
void Lexer::retract() {
    if (forward_ > 0) {
        --forward_;
        char c = buf_[activeBuf_][forward_];
        if (c == '\n') {
            --line_;
            col_ = prevCol_; // restore column to where it was before the '\n'
        } else {
            col_ = prevCol_;
        }
    }
}

// =============================================================================
// skipWhitespace — eat spaces, tabs, carriage-returns, newlines, and comments
// =============================================================================
void Lexer::skipWhitespace() {
    while (true) {
        char c = nextChar();
        if (c == '\0') return;

        if (c == '{') {
            // Start of a block comment
            skipComment(line_, col_);
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c)))
            continue;

        // Non-whitespace, non-comment: put it back
        retract();
        return;
    }
}

// =============================================================================
// skipComment — opening '{' already consumed
// =============================================================================
void Lexer::skipComment(int startLine, int startCol) {
    while (true) {
        char c = nextChar();
        if (c == '\0') {
            ErrorHandler::instance().lexError(
                startLine, startCol,
                "Unterminated comment (opened here)");
            return;
        }
        if (c == '}') return; // comment closed
        if (c == '{') {
            // Nested comments are not allowed in this Pascal subset.
            // Report but keep scanning to the end of the outer comment.
            ErrorHandler::instance().lexError(
                line_, col_,
                "Nested comments are not allowed");
        }
    }
}

// =============================================================================
// scanIdentifierOrKeyword
// 'first' is the character that was already consumed (an alpha or '_').
// =============================================================================
Token Lexer::scanIdentifierOrKeyword(char first, int startLine, int startCol) {
    std::string lexeme(1, first);

    while (true) {
        char c = nextChar();
        if (c == '\0') break;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            lexeme += c;
        else {
            retract();
            break;
        }
    }

    // Case-insensitive keyword lookup
    std::string lower = lexeme;
    for (char& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    auto it = KEYWORDS.find(lower);
    if (it != KEYWORDS.end())
        return Token(it->second, lexeme, startLine, startCol);

    return Token(TokenType::ID, lexeme, startLine, startCol);
}

// =============================================================================
// scanNumber
// 'first' is the first digit character, already consumed.
//
// Recognised forms:
//   integer       : digit+
//   real          : digit+ . digit+
//   real w/ exp   : digit+ [. digit+] (e|E) [+|-] digit+
//
// Edge case: digit+ followed by ".." — the ".." is NOT part of this number.
// We detect this by peeking one char after the dot.
// =============================================================================
Token Lexer::scanNumber(char first, int startLine, int startCol) {
    std::string lexeme(1, first);

    // ---- integer part -------------------------------------------------------
    while (true) {
        char c = nextChar();
        if (c == '\0') break;
        if (std::isdigit(static_cast<unsigned char>(c))) { lexeme += c; continue; }
        retract();
        break;
    }

    // ---- optional fractional part -------------------------------------------
    {
        char dot = nextChar();
        if (dot == '.') {
            // Look one more character ahead to distinguish  3.14  from  1..10
            char after = nextChar();
            if (after == '.') {
                // It's a ".." range operator — put both chars back
                retract(); // put back second '.'
                retract(); // put back first  '.'
                // Fall through without consuming either '.'
                goto exponent_check;
            }
            // It really is a decimal point
            lexeme += '.';
            if (std::isdigit(static_cast<unsigned char>(after))) {
                lexeme += after;
                while (true) {
                    char c = nextChar();
                    if (c == '\0') break;
                    if (std::isdigit(static_cast<unsigned char>(c))) { lexeme += c; continue; }
                    retract();
                    break;
                }
            } else {
                // Digit required after decimal point
                if (after != '\0') retract(); // put non-digit back
                ErrorHandler::instance().lexError(
                    line_, col_,
                    "Expected digit after decimal point in numeric literal");
            }
        } else {
            if (dot != '\0') retract();
        }
    }

    exponent_check:
    // ---- optional exponent part ---------------------------------------------
    {
        char e = nextChar();
        if (e == 'e' || e == 'E') {
            lexeme += e;
            char sign = nextChar();
            if (sign == '+' || sign == '-') {
                lexeme += sign;
                sign = nextChar(); // reuse var as first digit
            }
            // sign now holds the first char after the optional sign
            if (!std::isdigit(static_cast<unsigned char>(sign))) {
                ErrorHandler::instance().lexError(
                    line_, col_,
                    "Expected digit after exponent in numeric literal");
                if (sign != '\0') retract();
            } else {
                lexeme += sign;
                while (true) {
                    char c = nextChar();
                    if (c == '\0') break;
                    if (std::isdigit(static_cast<unsigned char>(c))) { lexeme += c; continue; }
                    retract();
                    break;
                }
            }
        } else {
            if (e != '\0') retract();
        }
    }

    return Token(TokenType::NUM, lexeme, startLine, startCol);
}

// =============================================================================
// scanToken — the main DFA dispatcher
// =============================================================================
Token Lexer::scanToken() {
    skipWhitespace();

    // Latch the position of the first character of this token
    // (col_ at this point is the position of the last whitespace char;
    //  the actual start position is captured right after nextChar() below)
    char c = nextChar();
    if (c == '\0')
        return Token(TokenType::EOF_TOKEN, "EOF", line_, col_ + 1);

    int startLine = line_;
    int startCol  = col_;

    // ---- Identifiers and keywords -------------------------------------------
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        return scanIdentifierOrKeyword(c, startLine, startCol);

    // ---- Numbers ------------------------------------------------------------
    if (std::isdigit(static_cast<unsigned char>(c)))
        return scanNumber(c, startLine, startCol);

    // ---- Operators and punctuation ------------------------------------------
    switch (c) {

        // assignop / colon
        case ':': {
            char next = nextChar();
            if (next == '=')
                return Token(TokenType::ASSIGNOP, ":=", startLine, startCol);
            if (next != '\0') retract();
            return Token(TokenType::COLON, ":", startLine, startCol);
        }

        // relop: < | <= | <>
        case '<': {
            char next = nextChar();
            if (next == '>')
                return Token(TokenType::RELOP_NEQ, "<>", startLine, startCol);
            if (next == '=')
                return Token(TokenType::RELOP_LE,  "<=", startLine, startCol);
            if (next != '\0') retract();
            return Token(TokenType::RELOP_LT, "<", startLine, startCol);
        }

        // relop: > | >=
        case '>': {
            char next = nextChar();
            if (next == '=')
                return Token(TokenType::RELOP_GE, ">=", startLine, startCol);
            if (next != '\0') retract();
            return Token(TokenType::RELOP_GT, ">", startLine, startCol);
        }

        // dot | dotdot
        case '.': {
            char next = nextChar();
            if (next == '.')
                return Token(TokenType::DOTDOT, "..", startLine, startCol);
            if (next != '\0') retract();
            return Token(TokenType::DOT, ".", startLine, startCol);
        }

        // relop: =
        case '=':  return Token(TokenType::RELOP_EQ,    "=",  startLine, startCol);

        // addop
        case '+':  return Token(TokenType::ADDOP_PLUS,  "+",  startLine, startCol);
        case '-':  return Token(TokenType::ADDOP_MINUS, "-",  startLine, startCol);

        // mulop
        case '*':  return Token(TokenType::MULOP_STAR,  "*",  startLine, startCol);
        case '/':  return Token(TokenType::MULOP_SLASH, "/",  startLine, startCol);

        // punctuation
        case '(':  return Token(TokenType::LPAREN,    "(",  startLine, startCol);
        case ')':  return Token(TokenType::RPAREN,    ")",  startLine, startCol);
        case '[':  return Token(TokenType::LBRACKET,  "[",  startLine, startCol);
        case ']':  return Token(TokenType::RBRACKET,  "]",  startLine, startCol);
        case ',':  return Token(TokenType::COMMA,     ",",  startLine, startCol);
        case ';':  return Token(TokenType::SEMICOLON, ";",  startLine, startCol);

        // ---- Unknown character ----------------------------------------------
        default: {
            std::string msg = "Unrecognized character '";
            msg += c;
            msg += "' (ASCII ";
            msg += std::to_string(static_cast<unsigned char>(c));
            msg += ")";
            ErrorHandler::instance().lexError(startLine, startCol, msg);
            return Token(TokenType::UNKNOWN, std::string(1, c), startLine, startCol);
        }
    }
}
