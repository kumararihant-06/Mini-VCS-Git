/**
 * cmd_commit.cpp
 * --------------
 *
 *   Strategy 1 – FULL_COPY
 *     Every file and every tree is physically stored on every commit with
 *     NO deduplication.  Storage grows linearly: 10 commits = ~10x C1 storage.
 *     Blobs and tree objects are written into a per-commit sub-directory
 *     (.git/objects/s1/<commit_idx>/) so identical content always produces
 *     new on-disk files.  The standard object store is also updated so
 *     checkout works normally.  S1 reads every file every commit (no cache),
 *     making it the simplest implementation.
 *
 *   Strategy 2 – DELTA_METADATA
 *     Only files whose mtime+size changed get a new real blob.  Unchanged
 *     files get a tiny sentinel "ref:<first_commit_that_stored_it>" blob.
 *     Storage stays near-flat when nothing changes.  Slightly slower than S1
 *     because it performs stat() + cache lookup per file.
 *
 *   Strategy 3 – DELTA_HASH
 *     Uses the same metadata cache as S2 for fast unchanged-file detection.
 *     For files whose metadata changed, reads content and compares SHA-1 to
 *     confirm actual change.  Unchanged files get the same sentinel as S2.
 *     Storage and time are comparable to S2; slightly more accurate.
 */

#include "cmd_commit.h"
#include "objects.h"
#include "repo_config.h"
#include "utils.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>

namespace fs   = std::filesystem;
namespace clk  = std::chrono;
using SteadyTP = clk::time_point<clk::steady_clock>;

static const std::string DEFAULT_AUTHOR = "MiniVCS User <user@minivcs.com>";

// ── Helpers ───────────────────────────────────────────────────────────────────

static SteadyTP now() { return clk::steady_clock::now(); }

static long long ms_between(SteadyTP start, SteadyTP end) {
    return clk::duration_cast<clk::milliseconds>(end - start).count();
}

static size_t object_file_size(const Repository& repo, const std::string& hash) {
    fs::path p = repo.objects_dir / hash.substr(0, 2) / hash.substr(2);
    if (fs::exists(p)) return (size_t)fs::file_size(p);
    return 0;
}

/**
 * Walk the commit chain from `tip` and return the count.
 * Used by S1 to produce a unique per-commit namespace index.
 */
static int count_commits(const Repository& repo, const std::string& tip) {
    int n = 0;
    std::string cur = tip;
    while (!cur.empty()) {
        ++n;
        try {
            auto cd = CommitData::from_content(repo.load_object(cur).content);
            cur = cd.parent_hashes.empty() ? "" : cd.parent_hashes[0];
        } catch (...) { break; }
    }
    return n;
}

/**
 * S1: Force-write any GitObject into a per-commit namespace directory.
 * Always creates a new physical file — guarantees true duplication even
 * for identical content.  Returns bytes written.
 */
static size_t s1_force_write(const Repository&  repo,
                              const GitObject&   obj,
                              int                commit_idx)
{
    std::string h    = obj.hash();
    fs::path    dir  = repo.git_dir / "objects" / "s1" /
                       std::to_string(commit_idx) / h.substr(0, 2);
    fs::path    file = dir / h.substr(2);
    fs::create_directories(dir);
    write_file(file, obj.serialize());   // always writes → true duplication
    return (size_t)fs::file_size(file);
}

/**
 * S1: Store object in standard location (for checkout) AND in per-commit
 * namespace (for true duplication in storage measurement).
 * Returns bytes written to per-commit namespace.
 */
static size_t s1_store(const Repository& repo,
                       const GitObject&  obj,
                       int               commit_idx)
{
    repo.store_object(obj);                        // standard store (checkout needs this)
    return s1_force_write(repo, obj, commit_idx);  // per-commit duplicate
}

/**
 * Resolve a sentinel blob ("ref:<commit>") to the real content blob hash.
 */
static std::string resolve_real_blob(const Repository&  repo,
                                     const std::string& blob_hash,
                                     const std::string& rel,
                                     int                max_hops = 64)
{
    std::string cur = blob_hash;
    for (int hop = 0; hop < max_hops; ++hop) {
        try {
            auto obj = repo.load_object(cur);
            if (obj.content.rfind("ref:", 0) != 0)
                return cur;
            std::string ref_commit = obj.content.substr(4);
            if (ref_commit.empty()) return cur;
            auto ref_pd   = CommitData::from_content(repo.load_object(ref_commit).content);
            auto ref_tree = repo.index_from_tree(ref_pd.tree_hash);
            if (!ref_tree.count(rel)) return cur;
            cur = ref_tree.at(rel);
        } catch (...) { break; }
    }
    return cur;
}

/**
 * Build a Tree hierarchy from a flat { path -> blob_hash } map.
 * S1 version: also force-writes every tree node into per-commit namespace.
 */
static std::string build_and_store_tree_s1(
    Repository& repo,
    const std::map<std::string, std::string>& file_to_blob,
    int commit_idx,
    size_t& bytes_stored)
{
    struct Node {
        std::optional<std::string>  blob_hash;
        std::map<std::string, Node> children;
    };
    Node root_node;

    for (const auto& [path, hash] : file_to_blob) {
        auto  parts = split(path, '/');
        Node* cur   = &root_node;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i + 1 == parts.size())
                cur->children[parts[i]].blob_hash = hash;
            else
                cur = &cur->children[parts[i]];
        }
    }

    std::function<std::string(const Node&)> build = [&](const Node& node) -> std::string {
        Tree t;
        for (const auto& [name, child] : node.children) {
            if (child.blob_hash) t.add_entry("100644", name, *child.blob_hash);
            else                  t.add_entry("40000",  name, build(child));
        }
        t.content = t.serialize_entries();
        // Force-write tree into per-commit namespace too
        bytes_stored += s1_force_write(repo, t, commit_idx);
        repo.store_object(t);  // standard store for checkout
        return t.hash();
    };
    return build(root_node);
}

/**
 * Standard build_and_store_tree for S2/S3.
 */
static std::string build_and_store_tree(
    Repository& repo,
    const std::map<std::string, std::string>& file_to_blob)
{
    struct Node {
        std::optional<std::string>  blob_hash;
        std::map<std::string, Node> children;
    };
    Node root_node;

    for (const auto& [path, hash] : file_to_blob) {
        auto  parts = split(path, '/');
        Node* cur   = &root_node;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i + 1 == parts.size())
                cur->children[parts[i]].blob_hash = hash;
            else
                cur = &cur->children[parts[i]];
        }
    }

    std::function<std::string(const Node&)> build = [&](const Node& node) -> std::string {
        Tree t;
        for (const auto& [name, child] : node.children) {
            if (child.blob_hash) t.add_entry("100644", name, *child.blob_hash);
            else                  t.add_entry("40000",  name, build(child));
        }
        t.content = t.serialize_entries();
        return repo.store_object(t);
    };
    return build(root_node);
}

/**
 * Create Commit object, advance branch pointer, save canonical index.
 */
static CommitResult finalise_commit(
    Repository&                               repo,
    const std::string&                        author,
    const std::string&                        message,
    const std::string&                        tree_hash,
    const std::string&                        parent_hash,
    int                                       files_stored,
    int                                       files_referenced,
    size_t                                    bytes_stored,
    SteadyTP                                  t_start,
    const std::map<std::string,std::string>&  canonical_index)
{
    CommitData cd;
    cd.tree_hash  = tree_hash;
    cd.author     = author.empty() ? DEFAULT_AUTHOR : author;
    cd.committer  = cd.author;
    cd.message    = message;
    cd.timestamp  = (long long)clk::system_clock::to_time_t(clk::system_clock::now());
    if (!parent_hash.empty()) cd.parent_hashes.push_back(parent_hash);

    auto        obj  = cd.to_object();
    std::string hash = repo.store_object(obj);

    repo.set_branch_commit(repo.current_branch(), hash);
    repo.save_index(canonical_index);

    CommitResult r;
    r.commit_hash      = hash;
    r.elapsed_ms       = ms_between(t_start, now());
    r.bytes_stored     = bytes_stored + object_file_size(repo, hash);
    r.files_stored     = files_stored;
    r.files_referenced = files_referenced;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Strategy 1 – FULL_COPY
// ─────────────────────────────────────────────────────────────────────────────

static CommitResult commit_full_copy(Repository&        repo,
                                     const std::string& message,
                                     const std::string& author)
{
    SteadyTP t0 = now();

    auto idx = repo.load_index();
    if (idx.empty()) {
        std::cout << "nothing to commit, working tree clean\n";
        return {};
    }

    std::string branch     = repo.current_branch();
    std::string parent     = repo.get_branch_commit(branch);
    int         commit_idx = count_commits(repo, parent);

    std::map<std::string, std::string> file_to_blob;
    std::map<std::string, std::string> canonical_index;
    int    files_stored = 0;
    size_t bytes_stored = 0;

    for (const auto& [rel, _ignored] : idx) {
        fs::path    full    = repo.root / rel;
        std::string content = read_file(full);
        Blob        blob(content);

        // Force-store blob: standard store (checkout) + per-commit namespace (duplication)
        bytes_stored += s1_store(repo, blob, commit_idx);

        std::string h        = blob.hash();
        file_to_blob[rel]    = h;
        canonical_index[rel] = h;
        ++files_stored;
    }

    // Build tree and also duplicate it in per-commit namespace
    std::string tree_hash = build_and_store_tree_s1(repo, file_to_blob,
                                                     commit_idx, bytes_stored);

    return finalise_commit(repo, author, message, tree_hash, parent,
                           files_stored, 0, bytes_stored, t0, canonical_index);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Strategy 2 – DELTA_METADATA
// ─────────────────────────────────────────────────────────────────────────────

static CommitResult commit_delta_metadata(Repository&        repo,
                                          const std::string& message,
                                          const std::string& author)
{
    SteadyTP t0 = now();

    auto idx = repo.load_index();
    if (idx.empty()) {
        std::cout << "nothing to commit, working tree clean\n";
        return {};
    }

    std::string branch = repo.current_branch();
    std::string parent = repo.get_branch_commit(branch);

    MetaCache cache = load_meta_cache(repo.git_dir);

    std::map<std::string, std::string> file_to_blob;
    std::map<std::string, std::string> canonical_index;
    MetaCache new_cache;
    int    files_stored     = 0;
    int    files_referenced = 0;
    size_t bytes_stored     = 0;

    for (const auto& [rel, idx_blob_hash] : idx) {
        fs::path full = repo.root / rel;
        FileMeta live = read_file_meta(full, parent);

        bool changed = true;
        if (!parent.empty() && cache.count(rel)) {
            const FileMeta& cached = cache.at(rel);
            if (cached.mtime_sec  == live.mtime_sec &&
                cached.size_bytes == live.size_bytes)
                changed = false;
        }

        if (changed) {
            Blob        blob(read_file(full));
            std::string h        = blob.hash();
            fs::path    obj_path = repo.objects_dir / h.substr(0, 2) / h.substr(2);
            bool        is_new   = !fs::exists(obj_path);
            repo.store_object(blob);
            if (is_new) bytes_stored += object_file_size(repo, h);
            file_to_blob[rel]    = h;
            canonical_index[rel] = h;
            new_cache[rel]       = {live.mtime_sec, live.size_bytes, ""};
            ++files_stored;
        } else {
            const std::string& last_real    = cache.at(rel).last_commit_hash;
            std::string        sent_content = "ref:" + (last_real.empty() ? parent : last_real);
            Blob               sentinel(sent_content);
            std::string        sh       = sentinel.hash();
            fs::path           obj_path = repo.objects_dir / sh.substr(0, 2) / sh.substr(2);
            bool               is_new   = !fs::exists(obj_path);
            repo.store_object(sentinel);
            if (is_new) bytes_stored += object_file_size(repo, sh);
            file_to_blob[rel] = sh;

            std::string real_hash = idx_blob_hash;
            if (!last_real.empty()) {
                try {
                    auto ref_pd  = CommitData::from_content(repo.load_object(last_real).content);
                    auto ref_idx = repo.index_from_tree(ref_pd.tree_hash);
                    if (ref_idx.count(rel))
                        real_hash = resolve_real_blob(repo, ref_idx.at(rel), rel);
                } catch (...) {}
            }
            canonical_index[rel] = real_hash;
            new_cache[rel]       = cache.at(rel);
            ++files_referenced;
        }
    }

    std::string tree_hash = build_and_store_tree(repo, file_to_blob);

    if (!parent.empty()) {
        auto pd = CommitData::from_content(repo.load_object(parent).content);
        if (tree_hash == pd.tree_hash) {
            std::cout << "nothing to commit, working tree clean\n";
            return {};
        }
    }

    CommitResult result = finalise_commit(repo, author, message, tree_hash, parent,
                                          files_stored, files_referenced,
                                          bytes_stored, t0, canonical_index);

    for (auto& [rel, m] : new_cache)
        if (m.last_commit_hash.empty())
            m.last_commit_hash = result.commit_hash;
    save_meta_cache(repo.git_dir, new_cache);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Strategy 3 – DELTA_HASH
// ─────────────────────────────────────────────────────────────────────────────

static CommitResult commit_delta_hash(Repository&        repo,
                                      const std::string& message,
                                      const std::string& author)
{
    SteadyTP t0 = now();

    auto idx = repo.load_index();
    if (idx.empty()) {
        std::cout << "nothing to commit, working tree clean\n";
        return {};
    }

    std::string branch = repo.current_branch();
    std::string parent = repo.get_branch_commit(branch);

    MetaCache cache = load_meta_cache(repo.git_dir);

    std::map<std::string, std::string> file_to_blob;
    std::map<std::string, std::string> canonical_index;
    MetaCache new_cache;
    int    files_stored     = 0;
    int    files_referenced = 0;
    size_t bytes_stored     = 0;

    for (const auto& [rel, idx_blob_hash] : idx) {
        fs::path full = repo.root / rel;
        FileMeta live = read_file_meta(full, parent);

        // Phase 1: metadata check to skip reading unchanged files (fast path).
        bool meta_changed = true;
        if (!parent.empty() && cache.count(rel)) {
            const FileMeta& cached = cache.at(rel);
            if (cached.mtime_sec  == live.mtime_sec &&
                cached.size_bytes == live.size_bytes)
                meta_changed = false;
        }

        bool changed = true;

        if (!meta_changed) {
            // Metadata unchanged → skip file read, treat as unchanged.
            changed = false;
        } else if (!parent.empty() && cache.count(rel)) {
            // Metadata changed → read and compare hash to confirm.
            std::string content  = read_file(full);
            Blob        new_blob(content);
            std::string new_hash = new_blob.hash();

            const std::string& last_real = cache.at(rel).last_commit_hash;
            std::string        prev_real = idx_blob_hash;
            if (!last_real.empty()) {
                try {
                    auto ref_pd  = CommitData::from_content(repo.load_object(last_real).content);
                    auto ref_idx = repo.index_from_tree(ref_pd.tree_hash);
                    if (ref_idx.count(rel))
                        prev_real = resolve_real_blob(repo, ref_idx.at(rel), rel);
                } catch (...) {}
            }

            if (new_hash == prev_real) {
                changed = false;
            } else {
                fs::path obj_path = repo.objects_dir / new_hash.substr(0, 2) / new_hash.substr(2);
                bool     is_new   = !fs::exists(obj_path);
                repo.store_object(new_blob);
                if (is_new) bytes_stored += object_file_size(repo, new_hash);
                file_to_blob[rel]    = new_hash;
                canonical_index[rel] = new_hash;
                new_cache[rel]       = {live.mtime_sec, live.size_bytes, ""};
                ++files_stored;
                continue;
            }
        } else if (parent.empty()) {
            // First commit: store everything.
            std::string content  = read_file(full);
            Blob        new_blob(content);
            std::string new_hash = new_blob.hash();
            fs::path    obj_path = repo.objects_dir / new_hash.substr(0, 2) / new_hash.substr(2);
            bool        is_new   = !fs::exists(obj_path);
            repo.store_object(new_blob);
            if (is_new) bytes_stored += object_file_size(repo, new_hash);
            file_to_blob[rel]    = new_hash;
            canonical_index[rel] = new_hash;
            new_cache[rel]       = {live.mtime_sec, live.size_bytes, ""};
            ++files_stored;
            continue;
        }

        if (!changed) {
            const std::string& last_real    = cache.count(rel) ? cache.at(rel).last_commit_hash : "";
            std::string        sent_content = "ref:" + (last_real.empty() ? parent : last_real);
            Blob               sentinel(sent_content);
            std::string        sh       = sentinel.hash();
            fs::path           obj_path = repo.objects_dir / sh.substr(0, 2) / sh.substr(2);
            bool               is_new   = !fs::exists(obj_path);
            repo.store_object(sentinel);
            if (is_new) bytes_stored += object_file_size(repo, sh);
            file_to_blob[rel] = sh;

            std::string real_hash = idx_blob_hash;
            if (!last_real.empty()) {
                try {
                    auto ref_pd  = CommitData::from_content(repo.load_object(last_real).content);
                    auto ref_idx = repo.index_from_tree(ref_pd.tree_hash);
                    if (ref_idx.count(rel))
                        real_hash = resolve_real_blob(repo, ref_idx.at(rel), rel);
                } catch (...) {}
            }
            canonical_index[rel] = real_hash;
            new_cache[rel]       = cache.count(rel) ? cache.at(rel)
                                                     : FileMeta{live.mtime_sec, live.size_bytes, ""};
            ++files_referenced;
        }
    }

    std::string tree_hash = build_and_store_tree(repo, file_to_blob);

    if (!parent.empty()) {
        auto pd = CommitData::from_content(repo.load_object(parent).content);
        if (tree_hash == pd.tree_hash) {
            std::cout << "nothing to commit, working tree clean\n";
            return {};
        }
    }

    CommitResult result = finalise_commit(repo, author, message, tree_hash, parent,
                                          files_stored, files_referenced,
                                          bytes_stored, t0, canonical_index);

    for (auto& [rel, m] : new_cache)
        if (m.last_commit_hash.empty())
            m.last_commit_hash = result.commit_hash;
    save_meta_cache(repo.git_dir, new_cache);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public entry point
// ─────────────────────────────────────────────────────────────────────────────

CommitResult cmd_commit(Repository&        repo,
                        const std::string& message,
                        const std::string& author)
{
    CommitStrategy strategy = load_config(repo.git_dir);

    CommitResult result;
    switch (strategy) {
        case CommitStrategy::FULL_COPY:
            result = commit_full_copy(repo, message, author);
            break;
        case CommitStrategy::DELTA_METADATA:
            result = commit_delta_metadata(repo, message, author);
            break;
        case CommitStrategy::DELTA_HASH:
            result = commit_delta_hash(repo, message, author);
            break;
    }

    print_commit_result(result, strategy);
    return result;
}