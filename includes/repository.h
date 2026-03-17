#pragma once
/**
 * repository.h
 * ------------
 * The Repository class holds all path references for a .git/ directory
 * and exposes low-level object-store operations shared by every command.
 *
 * Higher-level commands (add, commit, checkout, branch, log, status)
 * are implemented in their own modules but receive a Repository& so they
 * can read/write objects and the index.
 */

#include "objects.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;

class Repository {
public:
    fs::path root;
    fs::path git_dir;
    fs::path objects_dir;
    fs::path refs_dir;
    fs::path heads_dir;
    fs::path head_file;
    fs::path index_file;

    explicit Repository(const std::string& path = ".");

    // ── Object store ──────────────────────────────────────────────────────────

    /** Write object to .git/objects/<xx>/<38> and return its SHA-1 hex. */
    std::string store_object(const GitObject& obj) const;

    /** Load and deserialize an object by its 40-char hex hash. */
    GitObject load_object(const std::string& hash_hex) const;

    // ── Index (staging area) ──────────────────────────────────────────────────

    std::map<std::string, std::string> load_index() const;
    void save_index(const std::map<std::string, std::string>& idx) const;

    // ── Branch / HEAD helpers ─────────────────────────────────────────────────

    /** Name of currently checked-out branch (from HEAD). */
    std::string current_branch() const;

    /** Latest commit hash on a branch (empty string if none). */
    std::string get_branch_commit(const std::string& branch) const;

    /** Write a commit hash to a branch ref file. */
    void set_branch_commit(const std::string& branch, const std::string& hash) const;

    // ── Tree helpers shared by commit / checkout / status ────────────────────

    /** Recursively collect all file paths reachable from a tree hash. */
    std::set<std::string> files_from_tree(const std::string& tree_hash,
                                          const std::string& prefix = "") const;

    /** Recursively build { path → blob-hash } map from a tree hash. */
    std::map<std::string, std::string> index_from_tree(const std::string& tree_hash,
                                                       const std::string& prefix = "") const;

    /** Restore all files in a tree into a filesystem directory. */
    void restore_tree(const std::string& tree_hash, const fs::path& base) const;

    /** Build a tree object hierarchy from the current index and store it.
     *  Returns the root tree hash. */
    std::string create_tree_from_index() const;
};
