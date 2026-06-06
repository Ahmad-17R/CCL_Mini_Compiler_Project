#pragma once
#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <vector>
#include <cstddef>  // size_t

// =============================================================================
// SymbolKind — what kind of entity this name refers to
// =============================================================================
enum class SymbolKind {
    VARIABLE,
    CONSTANT,
    PARAMETER,
    FUNCTION,
    PROCEDURE,
    ARRAY,
    PROGRAM
};

// =============================================================================
// SymType — the Pascal type of the entity
// =============================================================================
enum class SymType {
    TYPE_INTEGER,
    TYPE_REAL,
    TYPE_ARRAY,
    TYPE_VOID      // procedures, program name
};

// =============================================================================
// SymbolEntry — one record in the symbol table
//
// Fields
// ------
//   name        — identifier, stored lower-cased (Pascal is case-insensitive)
//   kind        — VARIABLE / CONSTANT / PARAMETER / FUNCTION / PROCEDURE /
//                 ARRAY / PROGRAM
//   type        — TYPE_INTEGER / TYPE_REAL / TYPE_ARRAY / TYPE_VOID
//   scope_level — 0 = global, 1 = first nested scope, …
//   line        — source line of declaration
//   col         — source column of declaration
//   array_start — lower bound of array (meaningful when kind == ARRAY)
//   array_end   — upper bound of array (meaningful when kind == ARRAY)
//   param_count — number of parameters (meaningful for FUNCTION / PROCEDURE)
// =============================================================================
struct SymbolEntry {
    std::string name;
    SymbolKind  kind        = SymbolKind::VARIABLE;
    SymType     type        = SymType::TYPE_VOID;
    int         scope_level = 0;
    int         line        = 0;
    int         col         = 0;
    int         array_start = -1;
    int         array_end   = -1;
    int         param_count = 0;
};

// =============================================================================
// Helper — human-readable strings for dump()
// =============================================================================
inline const char* symKindToString(SymbolKind k) {
    switch (k) {
        case SymbolKind::VARIABLE:  return "VARIABLE";
        case SymbolKind::CONSTANT:  return "CONSTANT";
        case SymbolKind::PARAMETER: return "PARAMETER";
        case SymbolKind::FUNCTION:  return "FUNCTION";
        case SymbolKind::PROCEDURE: return "PROCEDURE";
        case SymbolKind::ARRAY:     return "ARRAY";
        case SymbolKind::PROGRAM:   return "PROGRAM";
        default:                    return "?KIND?";
    }
}

inline const char* symTypeToString(SymType t) {
    switch (t) {
        case SymType::TYPE_INTEGER: return "integer";
        case SymType::TYPE_REAL:    return "real";
        case SymType::TYPE_ARRAY:   return "array";
        case SymType::TYPE_VOID:    return "void";
        default:                    return "?TYPE?";
    }
}

// =============================================================================
// SymbolTable
//
// Implementation
// --------------
//   Single flat hash table with chaining (linked-list buckets).
//   Table size = HASH_SIZE (prime 211).
//
//   Scope is tracked by a scope-level integer stored inside each entry and
//   by a scope stack that records which bucket indices were dirtied at each
//   level so exitScope() can remove exactly those entries in O(entries added).
//
//   Collision resolution: separate chaining — each bucket is a singly-linked
//   list of Node objects owned by the table.
// =============================================================================
class SymbolTable {
public:
    // Prime table size for good hash distribution
    static constexpr std::size_t HASH_SIZE = 211;

    SymbolTable();
    ~SymbolTable();

    // Disallow copying (owns heap nodes)
    SymbolTable(const SymbolTable&)            = delete;
    SymbolTable& operator=(const SymbolTable&) = delete;

    // ---- scope management ---------------------------------------------------
    void enterScope();
    void exitScope();
    int  currentScope() const { return scopeLevel_; }

    // ---- CRUD ---------------------------------------------------------------

    // Insert entry into current scope.
    // Returns false (and reports an error) if the name is already declared
    // in the *current* scope.
    bool insert(const SymbolEntry& entry);

    // Lookup from innermost scope outward.
    // Returns pointer into the table's storage — valid until the entry's scope
    // is exited or the table is destroyed.  Returns nullptr if not found.
    SymbolEntry* lookup(const std::string& name);
    const SymbolEntry* lookup(const std::string& name) const;

    // Lookup only within the current (innermost) scope.
    SymbolEntry* lookupCurrentScope(const std::string& name);
    const SymbolEntry* lookupCurrentScope(const std::string& name) const;

    // Remove a specific entry by name and scope level.
    // Mainly used for cleanup; exitScope() handles bulk removal automatically.
    bool remove(const std::string& name, int scope_level);

    // ---- debug --------------------------------------------------------------
    // Prints the entire table grouped by scope level in a formatted table.
    void dump() const;

    // Returns the total number of entries currently in the table.
    int size() const;

private:
    // ---- internal node type -------------------------------------------------
    struct Node {
        SymbolEntry entry;
        Node*       next = nullptr;
    };

    // ---- storage ------------------------------------------------------------
    Node* buckets_[HASH_SIZE];  // hash-table array; each element is a chain head

    int   scopeLevel_;          // current scope depth (0 = global)

    // Scope stack: each element is the list of (bucket, node*) pairs inserted
    // at that scope level — used by exitScope() for fast cleanup.
    struct ScopeRecord {
        std::size_t bucketIdx;
        Node*       node;       // pointer to the node itself (not its predecessor)
    };
    std::vector<std::vector<ScopeRecord>> scopeStack_;

    // ---- helpers ------------------------------------------------------------
    static std::string normalise(const std::string& name);
    static std::size_t hash(const std::string& key);

    // Low-level node search within one bucket chain
    Node* findInChain(std::size_t bucketIdx,
                      const std::string& key,
                      int minScope, int maxScope) const;

    // Unlink and delete a node from a bucket chain
    bool removeFromBucket(std::size_t bucketIdx,
                          const std::string& key,
                          int scope_level);
};

#endif // SYMBOL_TABLE_H
