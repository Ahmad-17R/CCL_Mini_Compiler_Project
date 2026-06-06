#include "symbol_table.h"
#include "../errorhandler/error_handler.h"
#include "../common/globals.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <stdexcept>

// =============================================================================
// Constructor / Destructor
// =============================================================================
SymbolTable::SymbolTable() : scopeLevel_(0) {
    // Zero-initialise all bucket heads
    for (std::size_t i = 0; i < HASH_SIZE; ++i)
        buckets_[i] = nullptr;

    // Push scope 0 (global) onto the scope stack
    scopeStack_.emplace_back();  // empty record list for scope 0
}

SymbolTable::~SymbolTable() {
    // Free every node in every bucket
    for (std::size_t i = 0; i < HASH_SIZE; ++i) {
        Node* cur = buckets_[i];
        while (cur) {
            Node* next = cur->next;
            delete cur;
            cur = next;
        }
        buckets_[i] = nullptr;
    }
}

// =============================================================================
// normalise — lower-case the identifier (Pascal is case-insensitive)
// =============================================================================
std::string SymbolTable::normalise(const std::string& name) {
    std::string lower = name;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

// =============================================================================
// hash — djb2 variant, maps a lower-cased key to [0, HASH_SIZE)
// =============================================================================
std::size_t SymbolTable::hash(const std::string& key) {
    std::size_t h = 5381;
    for (unsigned char c : key)
        h = ((h << 5) + h) ^ static_cast<std::size_t>(c); // h*33 XOR c
    return h % HASH_SIZE;
}

// =============================================================================
// Scope management
// =============================================================================
void SymbolTable::enterScope() {
    ++scopeLevel_;
    scopeStack_.emplace_back(); // empty record list for the new scope
    if (g_verbose)
        std::cout << "[SymTab] enterScope() -> level " << scopeLevel_ << "\n";
}

void SymbolTable::exitScope() {
    if (scopeLevel_ == 0) {
        // Should never happen in a well-formed program, but guard it.
        ErrorHandler::instance().semError(
            0, 0, "[SymbolTable] exitScope() called at global scope — ignored");
        return;
    }

    if (g_verbose)
        std::cout << "[SymTab] exitScope() <- level " << scopeLevel_ << "\n";

    // Remove every node that was inserted at this scope level
    for (const ScopeRecord& rec : scopeStack_.back())
        removeFromBucket(rec.bucketIdx, rec.node->entry.name, scopeLevel_);

    scopeStack_.pop_back();
    --scopeLevel_;
}

// =============================================================================
// insert
// =============================================================================
bool SymbolTable::insert(const SymbolEntry& entry) {
    std::string key        = normalise(entry.name);
    std::size_t bucketIdx  = hash(key);

    // Duplicate check: scan the bucket for any node with the same key AND
    // the same scope level (shadowing in a deeper scope is fine).
    Node* existing = findInChain(bucketIdx, key, scopeLevel_, scopeLevel_);
    if (existing) {
        ErrorHandler::instance().semError(
            entry.line, entry.col,
            "'" + entry.name + "' already declared in this scope "
            "(first declared at line " +
            std::to_string(existing->entry.line) + ")");
        return false;
    }

    // Build the new node
    Node* node        = new Node();
    node->entry       = entry;
    node->entry.name  = key;                // store normalised
    node->entry.scope_level = scopeLevel_;

    // Prepend to the bucket chain (O(1))
    node->next        = buckets_[bucketIdx];
    buckets_[bucketIdx] = node;

    // Record this insertion so exitScope() can find and delete it
    scopeStack_.back().push_back({ bucketIdx, node });

    if (g_verbose)
        std::cout << "[SymTab] insert: " << key
                  << "  scope=" << scopeLevel_
                  << "  kind="  << symKindToString(node->entry.kind)
                  << "  type="  << symTypeToString(node->entry.type)
                  << "\n";

    return true;
}

// =============================================================================
// lookup — innermost scope outward
// Returns pointer to the entry with the *highest* scope_level ≤ scopeLevel_.
// Because we prepend on insert, the most-recently-inserted (deepest) entry
// for a given name always appears first in the chain — a simple linear scan
// of the chain is sufficient.
// =============================================================================
SymbolEntry* SymbolTable::lookup(const std::string& name) {
    std::string key       = normalise(name);
    std::size_t bucketIdx = hash(key);
    Node* node = findInChain(bucketIdx, key, 0, scopeLevel_);
    return node ? &node->entry : nullptr;
}

const SymbolEntry* SymbolTable::lookup(const std::string& name) const {
    std::string key       = normalise(name);
    std::size_t bucketIdx = hash(key);
    Node* node = findInChain(bucketIdx, key, 0, scopeLevel_);
    return node ? &node->entry : nullptr;
}

// =============================================================================
// lookupCurrentScope
// =============================================================================
SymbolEntry* SymbolTable::lookupCurrentScope(const std::string& name) {
    std::string key       = normalise(name);
    std::size_t bucketIdx = hash(key);
    Node* node = findInChain(bucketIdx, key, scopeLevel_, scopeLevel_);
    return node ? &node->entry : nullptr;
}

const SymbolEntry* SymbolTable::lookupCurrentScope(const std::string& name) const {
    std::string key       = normalise(name);
    std::size_t bucketIdx = hash(key);
    Node* node = findInChain(bucketIdx, key, scopeLevel_, scopeLevel_);
    return node ? &node->entry : nullptr;
}

// =============================================================================
// remove — removes one entry by name + scope_level
// =============================================================================
bool SymbolTable::remove(const std::string& name, int scope_level) {
    std::string key       = normalise(name);
    std::size_t bucketIdx = hash(key);
    return removeFromBucket(bucketIdx, key, scope_level);
}

// =============================================================================
// dump — pretty-print the whole table grouped by scope level
// =============================================================================
void SymbolTable::dump() const {
    // Gather entries per scope level
    std::vector<std::vector<const SymbolEntry*>> byScope(
        static_cast<std::size_t>(scopeLevel_ + 1));

    for (std::size_t i = 0; i < HASH_SIZE; ++i) {
        for (Node* cur = buckets_[i]; cur; cur = cur->next) {
            int lvl = cur->entry.scope_level;
            if (lvl >= 0 && lvl <= scopeLevel_)
                byScope[static_cast<std::size_t>(lvl)].push_back(&cur->entry);
        }
    }

    // Sort each scope's entries by insertion line for readability
    for (auto& vec : byScope)
        std::sort(vec.begin(), vec.end(),
            [](const SymbolEntry* a, const SymbolEntry* b){
                return a->line < b->line;
            });

    // ---- header -------------------------------------------------------------
    const int W_NAME  = 18;
    const int W_KIND  = 12;
    const int W_TYPE  = 10;
    const int W_LINE  =  6;
    const int W_ARR   = 14;
    const int W_PARAM =  7;
    const int TOTAL   = W_NAME + W_KIND + W_TYPE + W_LINE + W_ARR + W_PARAM + 10;

    std::cout << "\n" << std::string(TOTAL, '=') << "\n";
    std::cout << "  SYMBOL TABLE DUMP\n";
    std::cout << std::string(TOTAL, '=') << "\n";

    for (int lvl = 0; lvl <= scopeLevel_; ++lvl) {
        std::cout << "\n  Scope level " << lvl
                  << " (" << byScope[static_cast<std::size_t>(lvl)].size()
                  << " entries)\n";
        std::cout << "  " << std::string(TOTAL - 2, '-') << "\n";
        std::cout << "  "
                  << std::left << std::setw(W_NAME)  << "Name"
                  << std::setw(W_KIND)  << "Kind"
                  << std::setw(W_TYPE)  << "Type"
                  << std::setw(W_LINE)  << "Line"
                  << std::setw(W_ARR)   << "Array[lo..hi]"
                  << std::setw(W_PARAM) << "Params"
                  << "\n";
        std::cout << "  " << std::string(TOTAL - 2, '-') << "\n";

        if (byScope[static_cast<std::size_t>(lvl)].empty()) {
            std::cout << "  (empty)\n";
            continue;
        }

        for (const SymbolEntry* e : byScope[static_cast<std::size_t>(lvl)]) {
            // Array bounds column
            std::string arrStr = "-";
            if (e->kind == SymbolKind::ARRAY ||
                (e->type == SymType::TYPE_ARRAY && e->array_start != -1))
                arrStr = "[" + std::to_string(e->array_start) +
                         ".." + std::to_string(e->array_end) + "]";

            // Param count column
            std::string paramStr = "-";
            if (e->kind == SymbolKind::FUNCTION ||
                e->kind == SymbolKind::PROCEDURE)
                paramStr = std::to_string(e->param_count);

            std::cout << "  "
                      << std::left
                      << std::setw(W_NAME)  << e->name
                      << std::setw(W_KIND)  << symKindToString(e->kind)
                      << std::setw(W_TYPE)  << symTypeToString(e->type)
                      << std::setw(W_LINE)  << e->line
                      << std::setw(W_ARR)   << arrStr
                      << std::setw(W_PARAM) << paramStr
                      << "\n";
        }
    }

    std::cout << "\n" << std::string(TOTAL, '=') << "\n\n";
}

// =============================================================================
// size — count total entries currently in the table
// =============================================================================
int SymbolTable::size() const {
    int count = 0;
    for (std::size_t i = 0; i < HASH_SIZE; ++i)
        for (Node* cur = buckets_[i]; cur; cur = cur->next)
            ++count;
    return count;
}

// =============================================================================
// findInChain — linear scan of one bucket for key with scope in [minS, maxS]
// Returns the node with the highest scope_level in that range (most local),
// or nullptr if not found.
// Because newer (deeper-scope) entries are prepended, the first match we find
// in the chain is already the deepest one — so we return on the first match.
// =============================================================================
SymbolTable::Node* SymbolTable::findInChain(std::size_t bucketIdx,
                                             const std::string& key,
                                             int minScope,
                                             int maxScope) const {
    for (Node* cur = buckets_[bucketIdx]; cur; cur = cur->next) {
        if (cur->entry.name == key &&
            cur->entry.scope_level >= minScope &&
            cur->entry.scope_level <= maxScope)
            return cur;
    }
    return nullptr;
}

// =============================================================================
// removeFromBucket — unlink and free the node matching key + scope_level
// =============================================================================
bool SymbolTable::removeFromBucket(std::size_t bucketIdx,
                                    const std::string& key,
                                    int scope_level) {
    Node* prev = nullptr;
    Node* cur  = buckets_[bucketIdx];

    while (cur) {
        if (cur->entry.name == key && cur->entry.scope_level == scope_level) {
            if (prev)
                prev->next = cur->next;
            else
                buckets_[bucketIdx] = cur->next;
            delete cur;
            return true;
        }
        prev = cur;
        cur  = cur->next;
    }
    return false; // not found — caller shouldn't treat this as an error
}
