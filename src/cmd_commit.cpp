/**
 * cmd_commit.cpp
 * --------------
 * Three commit strategies:
 *
 *   Strategy 1 – FULL_COPY
 *     Every staged file → new Blob, every commit.
 *
 *   Strategy 2 – DELTA_METADATA
 *     Reads mtime + size from the meta_cache.  Only files where
 *     mtime or size changed get a new Blob.  Unchanged files get a
 *     sentinel Blob whose content is "ref:<prev_commit_hash>".
 *
 *   Strategy 3 – DELTA_HASH
 *     Reads the previous commit's tree to find each file's last known
 *     blob hash.  Re-hashes the working file.  Only files where the
 *     hash changed get a new Blob.  Unchanged files get the same
 *     "ref:<prev_commit_hash>" sentinel.
 *
 * Checkout resolves "ref:…" sentinel blobs before writing to disk,
 * so the user always sees correct file content regardless of strategy.
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

/** Return current time for elapsed measurement. */
static SteadyTP now() { return clk::steady_clock::now(); }

/** Milliseconds between two steady_clock points. */
static long long ms_between(SteadyTP start, SteadyTP end) {
    return clk::duration_cast<clk::milliseconds>(end - start).count();
}

/** Disk space consumed by a single object file (compressed bytes). */
static size_t object_file_size(const Repository& repo, const std::string& hash) {
    fs::path p = repo.objects_dir / hash.substr(0, 2) / hash.substr(2);
    if (fs::exists(p)) return (size_t)fs::file_size(p);
    return 0;
}

/**
 * Build a Tree hierarchy from a flat { path → blob_hash } map and
 * store every new Tree node.  Returns root tree hash.
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
 * Finalize: create the Commit object, advance the branch pointer,
 * clear the index, and return a filled CommitResult.
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
    SteadyTP                                  t_start)
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
    repo.save_index({});

    CommitResult r;
    r.commit_hash      = hash;
    r.elapsed_ms       = ms_between(t_start, now());
    r.bytes_stored     = bytes_stored + object_file_size(repo, hash); // include commit obj
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

    // Read every staged file, create Blob, accumulate stats
    std::map<std::string, std::string> file_to_blob; // path → blob hash
    int    files_stored = 0;
    size_t bytes_stored = 0;

    for (const auto& [rel, _ignored] : idx) {
        fs::path full = repo.root / rel;
        Blob     blob(read_file(full));
        // store_object is idempotent; measure only newly written bytes
        std::string h = blob.hash();
        fs::path    obj_path = repo.objects_dir / h.substr(0, 2) / h.substr(2);
        bool        is_new   = !fs::exists(obj_path);

        repo.store_object(blob);

        if (is_new) bytes_stored += object_file_size(repo, h);
        file_to_blob[rel] = h;
        ++files_stored;
    }

    std::string branch     = repo.current_branch();
    std::string parent     = repo.get_branch_commit(branch);
    std::string tree_hash  = build_and_store_tree(repo, file_to_blob);

    // No-op check (same tree as parent)
    if (!parent.empty()) {
        auto pd = CommitData::from_content(repo.load_object(parent).content);
        if (tree_hash == pd.tree_hash) {
            std::cout << "nothing to commit, working tree clean\n";
            return {};
        }
    }

    return finalise_commit(repo, author, message, tree_hash, parent,
                           files_stored, 0, bytes_stored, t0);
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

    // Load metadata cache from previous commits
    MetaCache cache = load_meta_cache(repo.git_dir);

    std::map<std::string, std::string> file_to_blob;
    MetaCache new_cache;
    int    files_stored     = 0;
    int    files_referenced = 0;
    size_t bytes_stored     = 0;

    for (const auto& [rel, _ignored] : idx) {
        fs::path full = repo.root / rel;

        // Read live metadata
        FileMeta live = read_file_meta(full, parent);

        bool changed = true;
        if (!parent.empty() && cache.count(rel)) {
            const FileMeta& cached = cache.at(rel);
            // Unchanged if mtime AND size both match
            if (cached.mtime_sec  == live.mtime_sec &&
                cached.size_bytes == live.size_bytes)
                changed = false;
        }

        if (changed) {
            // Store actual file content
            Blob     blob(read_file(full));
            std::string h = blob.hash();
            fs::path    obj_path = repo.objects_dir / h.substr(0, 2) / h.substr(2);
            bool        is_new   = !fs::exists(obj_path);
            repo.store_object(blob);
            if (is_new) bytes_stored += object_file_size(repo, h);
            file_to_blob[rel] = h;
            new_cache[rel]    = {live.mtime_sec, live.size_bytes, parent.empty() ? "" : parent};
            ++files_stored;
        } else {
            // Store a tiny sentinel blob: "ref:<commit_hash>"
            std::string sentinel_content = "ref:" + cache.at(rel).last_commit_hash;
            Blob        sentinel(sentinel_content);
            std::string h = sentinel.hash();
            fs::path    obj_path = repo.objects_dir / h.substr(0, 2) / h.substr(2);
            bool        is_new   = !fs::exists(obj_path);
            repo.store_object(sentinel);
            if (is_new) bytes_stored += object_file_size(repo, h);
            file_to_blob[rel] = h;
            new_cache[rel]    = cache.at(rel); // preserve old metadata
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

    // Persist updated metadata cache
    save_meta_cache(repo.git_dir, new_cache);

    return finalise_commit(repo, author, message, tree_hash, parent,
                           files_stored, files_referenced, bytes_stored, t0);
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

    // Build { path → blob_hash } from the previous commit's tree
    std::map<std::string, std::string> prev_tree_index;
    if (!parent.empty()) {
        auto pd = CommitData::from_content(repo.load_object(parent).content);
        if (!pd.tree_hash.empty())
            prev_tree_index = repo.index_from_tree(pd.tree_hash);
    }

    std::map<std::string, std::string> file_to_blob;
    int    files_stored     = 0;
    int    files_referenced = 0;
    size_t bytes_stored     = 0;

    for (const auto& [rel, _ignored] : idx) {
        fs::path full = repo.root / rel;

        // Hash the file content now
        std::string content  = read_file(full);
        Blob        new_blob(content);
        std::string new_hash = new_blob.hash();

        // Compare against previous commit's blob hash for this file
        bool changed = true;
        if (!parent.empty() && prev_tree_index.count(rel)) {
            const std::string& old_hash = prev_tree_index.at(rel);

            // Resolve potential sentinel from a previous delta commit
            // (If old_hash points to a "ref:…" sentinel, the file was
            //  already unchanged then — we still compare actual hashes.)
            // We load the old blob to check if it's a sentinel:
            try {
                auto old_obj = repo.load_object(old_hash);
                if (old_obj.content.rfind("ref:", 0) == 0) {
                    // Previous commit used a reference; resolve the real hash
                    std::string ref_commit = old_obj.content.substr(4);
                    // Load real blob from the referenced commit's tree
                    if (!ref_commit.empty()) {
                        auto ref_pd = CommitData::from_content(
                            repo.load_object(ref_commit).content);
                        auto ref_tree = repo.index_from_tree(ref_pd.tree_hash);
                        if (ref_tree.count(rel)) {
                            // Compare new_hash against the real stored hash
                            changed = (new_hash != ref_tree.at(rel));
                        }
                    }
                } else {
                    // Normal blob: compare directly
                    changed = (new_hash != old_hash);
                }
            } catch (...) {
                changed = true; // if we can't load, re-store to be safe
            }
        }

        if (changed) {
            fs::path obj_path = repo.objects_dir / new_hash.substr(0, 2) / new_hash.substr(2);
            bool     is_new   = !fs::exists(obj_path);
            repo.store_object(new_blob);
            if (is_new) bytes_stored += object_file_size(repo, new_hash);
            file_to_blob[rel] = new_hash;
            ++files_stored;
        } else {
            // Store sentinel "ref:<prev_commit>"
            std::string sentinel_content = "ref:" + parent;
            Blob        sentinel(sentinel_content);
            std::string sh = sentinel.hash();
            fs::path    obj_path = repo.objects_dir / sh.substr(0, 2) / sh.substr(2);
            bool        is_new   = !fs::exists(obj_path);
            repo.store_object(sentinel);
            if (is_new) bytes_stored += object_file_size(repo, sh);
            file_to_blob[rel] = sh;
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

    return finalise_commit(repo, author, message, tree_hash, parent,
                           files_stored, files_referenced, bytes_stored, t0);
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
