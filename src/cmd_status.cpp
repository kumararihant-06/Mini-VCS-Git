/**
 * cmd_status.cpp
 * --------------
 * Implementation of `mini_vcs status`.
 */

#include "cmd_status.h"
#include "objects.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace fs = std::filesystem;

void cmd_status(const Repository& repo) {
    std::string branch = repo.current_branch();
    std::cout << "On branch " << branch << "\n";

    auto        idx   = repo.load_index();
    std::string cur_h = repo.get_branch_commit(branch);

    // Files recorded in the last commit
    std::map<std::string, std::string> committed;
    if (!cur_h.empty()) {
        try {
            auto obj  = repo.load_object(cur_h);
            auto data = CommitData::from_content(obj.content);
            if (!data.tree_hash.empty())
                committed = repo.index_from_tree(data.tree_hash);
        } catch (...) {}
    }

    // Files currently on disk (excluding .git/)
    std::map<std::string, std::string> working;
    for (const auto& entry : fs::recursive_directory_iterator(repo.root)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& p = entry.path();
        bool in_git = false;
        for (const auto& part : p) if (part == ".git") { in_git = true; break; }
        if (in_git) continue;
        std::string rel = fs::relative(p, repo.root).generic_string();
        try { Blob b(read_file(p)); working[rel] = b.hash(); } catch (...) {}
    }

    // ── Staged changes ────────────────────────────────────────────────────────
    std::vector<std::pair<std::string, std::string>> staged;
    std::set<std::string> all_keys;
    for (const auto& [k, _] : idx)       all_keys.insert(k);
    for (const auto& [k, _] : committed) all_keys.insert(k);

    for (const auto& fp : all_keys) {
        auto it_i = idx.find(fp);
        auto it_c = committed.find(fp);
        bool in_idx = (it_i != idx.end());
        bool in_com = (it_c != committed.end());
        if (in_idx && !in_com)
            staged.push_back({"new file", fp});
        else if (in_idx && in_com && it_i->second != it_c->second)
            staged.push_back({"modified", fp});
    }
    if (!staged.empty()) {
        std::cout << "\nChanges to be committed:\n";
        for (const auto& [s, f] : staged)
            std::cout << "   " << s << ": " << f << "\n";
    }

    // ── Unstaged modifications ────────────────────────────────────────────────
    std::vector<std::string> unstaged;
    for (const auto& [fp, wh] : working) {
    if (idx.count(fp) && idx.at(fp) != wh) {
        // file is staged but disk version is different from staged version
        unstaged.push_back(fp);
    } else if (!idx.count(fp) && committed.count(fp) && committed.at(fp) != wh) {
        // file is not staged but exists in last commit with different hash
        unstaged.push_back(fp);
    }
    }   
    if (!unstaged.empty()) {
        std::cout << "\nChanges not staged for commit:\n";
        for (const auto& f : unstaged) std::cout << "   modified: " << f << "\n";
    }

    // ── Untracked files ───────────────────────────────────────────────────────
    std::vector<std::string> untracked;
    for (const auto& [fp, _] : working)
        if (!idx.count(fp) && !committed.count(fp))
            untracked.push_back(fp);
    if (!untracked.empty()) {
        std::cout << "\nUntracked files:\n";
        for (const auto& f : untracked) std::cout << "   " << f << "\n";
    }

    // ── Deleted files ─────────────────────────────────────────────────────────
    std::vector<std::string> deleted;
    for (const auto& [fp, _] : idx)
        if (!working.count(fp))
            deleted.push_back(fp);
    if (!deleted.empty()) {
        std::cout << "\nDeleted files:\n";
        for (const auto& f : deleted) std::cout << "   deleted: " << f << "\n";
    }

    if (staged.empty() && unstaged.empty() && untracked.empty() && deleted.empty())
        std::cout << "\nnothing to commit, working tree clean\n";
}
