/**
 * cmd_branch.cpp
 * --------------
 * Implementation of `mini_vcs branch`.
 */

#include "cmd_branch.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

void cmd_branch(Repository& repo,
                const std::string& name,
                bool del) {
    std::string cur = repo.current_branch();

    // ── Delete mode ───────────────────────────────────────────────────────────
    if (del && !name.empty()) {
        fs::path bf = repo.heads_dir / name;
        if (fs::exists(bf)) {
            fs::remove(bf);
            std::cout << "Deleted branch " << name << "\n";
        } else {
            std::cout << "Branch " << name << " not found\n";
        }
        return;
    }

    // ── Create mode ───────────────────────────────────────────────────────────
    if (!name.empty()) {
        std::string cur_commit = repo.get_branch_commit(cur);
        if (!cur_commit.empty()) {
            repo.set_branch_commit(name, cur_commit);
            std::cout << "Created branch " << name << "\n";
        } else {
            std::cout << "No commits yet, cannot create a new branch\n";
        }
        return;
    }

    // ── List mode ─────────────────────────────────────────────────────────────
    std::vector<std::string> branches;
    for (const auto& entry : fs::directory_iterator(repo.heads_dir))
        if (entry.is_regular_file() && entry.path().filename().string()[0] != '.')
            branches.push_back(entry.path().filename().string());

    std::sort(branches.begin(), branches.end());
    for (const auto& b : branches)
        std::cout << (b == cur ? "* " : "  ") << b << "\n";
}
