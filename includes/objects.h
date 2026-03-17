#pragma once
/**
 * objects.h
 * ---------
 * Core Git object model:
 *   GitObject  – base type for all stored objects (blob / tree / commit)
 *   Blob       – file content object
 *   TreeEntry  – single entry inside a Tree
 *   Tree       – directory snapshot object
 *   CommitData – parsed commit metadata + serialization helpers
 *
 * All objects follow the Git object format:
 *   stored as  zlib( "<type> <size>\0<content>" )
 *   identified by SHA-1 of the uncompressed bytes
 */

#include <optional>
#include <string>
#include <vector>

// ── GitObject ─────────────────────────────────────────────────────────────────

struct GitObject {
    std::string type;    // "blob" | "tree" | "commit"
    std::string content; // raw payload bytes

    GitObject() = default;
    GitObject(std::string t, std::string c);

    /** SHA-1( "<type> <size>\0<content>" ) → 40-char hex. */
    std::string hash() const;

    /** Serialize to zlib-compressed bytes ready for disk storage. */
    std::string serialize() const;

    /** Deserialize from raw (compressed) disk bytes. */
    static GitObject deserialize(const std::string& data);
};

// ── Blob ──────────────────────────────────────────────────────────────────────

/** A Blob stores raw file content. */
struct Blob : GitObject {
    explicit Blob(const std::string& bytes);
};

// ── Tree ──────────────────────────────────────────────────────────────────────

/** One entry inside a tree (file or sub-tree). */
struct TreeEntry {
    std::string mode;     // "100644" for files, "40000" for sub-trees
    std::string name;
    std::string obj_hash; // 40-char hex SHA-1
};

/** A Tree is an ordered list of TreeEntry items (one directory snapshot). */
struct Tree : GitObject {
    std::vector<TreeEntry> entries;

    Tree();

    /** Append an entry and rebuild the binary content. */
    void add_entry(const std::string& mode,
                   const std::string& name,
                   const std::string& hash_hex);

    /** Produce binary tree content (entries sorted by name). */
    std::string serialize_entries() const;

    /** Parse binary tree content into a Tree. */
    static Tree from_content(const std::string& raw);
};

// ── CommitData ────────────────────────────────────────────────────────────────

/** Parsed representation of a commit object's content. */
struct CommitData {
    std::string              tree_hash;
    std::vector<std::string> parent_hashes;
    std::string              author;
    std::string              committer;
    std::string              message;
    long long                timestamp = 0;

    /** Wrap this data into a GitObject ready to be stored. */
    GitObject to_object() const;

    /** Parse raw commit content (the payload inside a GitObject). */
    static CommitData from_content(const std::string& raw);
};
