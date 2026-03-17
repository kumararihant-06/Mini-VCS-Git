/**
 * repository.cpp
 * --------------
 * Repository class: path setup, object store, index I/O,
 * branch helpers, and shared tree utilities.
 */

#include "repository.h"
#include "index.h"
#include "objects.h"
#include "utils.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

// ── Constructor ───────────────────────────────────────────────────────────────

Repository::Repository(const std::string& path) {
    root        = fs::absolute(path);
    git_dir     = root / ".git";
    objects_dir = git_dir / "objects";
    refs_dir    = git_dir / "refs";
    heads_dir   = refs_dir / "heads";
    head_file   = git_dir / "HEAD";
    index_file  = git_dir / "index";
}

// ── Object store ──────────────────────────────────────────────────────────────

std::string Repository::store_object(const GitObject& obj) const {
    std::string h    = obj.hash();
    fs::path    dir  = objects_dir / h.substr(0, 2);
    fs::path    file = dir / h.substr(2);
    if (!fs::exists(file)) {
        fs::create_directories(dir);
        write_file(file, obj.serialize());
    }
    return h;
}

GitObject Repository::load_object(const std::string& hash_hex) const {
    fs::path file = objects_dir / hash_hex.substr(0, 2) / hash_hex.substr(2);
    if (!fs::exists(file))
        throw std::runtime_error("Object not found: " + hash_hex);
    return GitObject::deserialize(read_file(file));
}

// ── Index ─────────────────────────────────────────────────────────────────────

std::map<std::string, std::string> Repository::load_index() const {
    return ::load_index(index_file);
}

void Repository::save_index(const std::map<std::string, std::string>& idx) const {
    ::save_index(index_file, idx);
}

// ── Branch / HEAD ─────────────────────────────────────────────────────────────

std::string Repository::current_branch() const {
    if (!fs::exists(head_file)) return "master";
    std::string h = trim(read_file(head_file));
    if (h.rfind("ref: refs/heads/", 0) == 0) return h.substr(16);
    return "HEAD"; // detached HEAD state
}

std::string Repository::get_branch_commit(const std::string& branch) const {
    fs::path bf = heads_dir / branch;
    if (!fs::exists(bf)) return "";
    return trim(read_file(bf));
}

void Repository::set_branch_commit(const std::string& branch,
                                   const std::string& hash) const {
    write_file(heads_dir / branch, hash + "\n");
}

// ── Tree helpers ──────────────────────────────────────────────────────────────

std::set<std::string> Repository::files_from_tree(const std::string& tree_hash,
                                                   const std::string& prefix) const {
    std::set<std::string> result;
    try {
        auto obj  = load_object(tree_hash);
        auto tree = Tree::from_content(obj.content);
        for (const auto& e : tree.entries) {
            std::string full = prefix + e.name;
            if (e.mode.rfind("100", 0) == 0) {
                result.insert(full);
            } else {
                auto sub = files_from_tree(e.obj_hash, full + "/");
                result.insert(sub.begin(), sub.end());
            }
        }
    } catch (...) {}
    return result;
}

std::map<std::string, std::string>
Repository::index_from_tree(const std::string& tree_hash,
                             const std::string& prefix) const {
    std::map<std::string, std::string> result;
    try {
        auto obj  = load_object(tree_hash);
        auto tree = Tree::from_content(obj.content);
        for (const auto& e : tree.entries) {
            std::string full = prefix + e.name;
            if (e.mode.rfind("100", 0) == 0) {
                result[full] = e.obj_hash;
            } else {
                auto sub = index_from_tree(e.obj_hash, full + "/");
                result.insert(sub.begin(), sub.end());
            }
        }
    } catch (...) {}
    return result;
}

void Repository::restore_tree(const std::string& tree_hash,
                               const fs::path&    base) const {
    auto obj  = load_object(tree_hash);
    auto tree = Tree::from_content(obj.content);
    for (const auto& e : tree.entries) {
        fs::path fp = base / e.name;
        if (e.mode.rfind("100", 0) == 0) {
            auto blob_obj = load_object(e.obj_hash);

            // ── Sentinel blob resolution (Strategies 2 & 3) ──────────────
            // A sentinel has content "ref:<commit_hash>".  Resolve it by
            // looking up the file in that commit's tree recursively.
            if (blob_obj.content.rfind("ref:", 0) == 0) {
                std::string ref_commit = blob_obj.content.substr(4);
                // Walk up ref chain (defensive: cap at 64 hops)
                for (int hop = 0; hop < 64 && !ref_commit.empty(); ++hop) {
                    try {
                        auto ref_cd = CommitData::from_content(
                            load_object(ref_commit).content);
                        auto ref_idx = index_from_tree(ref_cd.tree_hash);
                        // Find the relative path of this file under base
                        std::string rel = fs::relative(fp, root).generic_string();
                        if (ref_idx.count(rel)) {
                            auto real_blob = load_object(ref_idx.at(rel));
                            if (real_blob.content.rfind("ref:", 0) == 0) {
                                ref_commit = real_blob.content.substr(4);
                                continue; // follow chain
                            }
                            write_file(fp, real_blob.content);
                        }
                    } catch (...) {}
                    break;
                }
            } else {
                write_file(fp, blob_obj.content);
            }
        } else {
            fs::create_directories(fp);
            restore_tree(e.obj_hash, fp);
        }
    }
}

std::string Repository::create_tree_from_index() const {
    auto idx = load_index();
    if (idx.empty()) {
        Tree t;
        return store_object(t);
    }

    // Build a nested node tree mirroring the directory structure
    struct Node {
        std::optional<std::string>   blob_hash; // present for files
        std::map<std::string, Node>  children;  // present for directories
    };
    Node root_node;

    for (const auto& [path, hash] : idx) {
        auto   parts = split(path, '/');
        Node*  cur   = &root_node;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i + 1 == parts.size())
                cur->children[parts[i]].blob_hash = hash;
            else
                cur = &cur->children[parts[i]];
        }
    }

    // Recursively build and store Tree objects bottom-up
    std::function<std::string(const Node&)> build_tree = [&](const Node& node) -> std::string {
        Tree t;
        for (const auto& [name, child] : node.children) {
            if (child.blob_hash) {
                t.add_entry("100644", name, *child.blob_hash);
            } else {
                std::string sub = build_tree(child);
                t.add_entry("40000", name, sub);
            }
        }
        t.content = t.serialize_entries();
        return store_object(t);
    };

    return build_tree(root_node);
}
