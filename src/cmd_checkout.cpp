/**
 * cmd_checkout.cpp
 * ----------------
 * Implementation of `mini_vcs checkout`.
 */

#include "cmd_checkout.h"
#include "objects.h"
#include "utils.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void cmd_checkout(Repository& repo,
                  const std::string& branch,
                  bool create_new) {
    std::string prev_branch = repo.current_branch();

    // 1. Collect files tracked by the current branch (we'll remove them)
    std::set<std::string> to_clear;
    std::string prev_commit = repo.get_branch_commit(prev_branch);
    if (!prev_commit.empty()) {
        try {
            auto obj  = repo.load_object(prev_commit);
            auto data = CommitData::from_content(obj.content);
            if (!data.tree_hash.empty())
                to_clear = repo.files_from_tree(data.tree_hash);
        } catch (...) {}
    }

    // 2. Handle branch existence / creation
    fs::path bf = repo.heads_dir / branch;
    if (!fs::exists(bf)) {
        if (create_new) {
            if (!prev_commit.empty()) {
                repo.set_branch_commit(branch, prev_commit);
                std::cout << "Created new branch " << branch << "\n";
            } else {
                std::cout << "No commits yet, cannot create a branch\n";
                return;
            }
        } else {
            std::cout << "Branch '" << branch << "' not found.\n";
            std::cout << "Use 'mini_vcs checkout -b " << branch
                      << "' to create and switch to a new branch.\n";
            return;
        }
    }

    // 3. Update HEAD
    write_file(repo.head_file, "ref: refs/heads/" + branch + "\n");

    // 4. Remove files from the previous branch
    for (const auto& rel : to_clear) {
        fs::path fp = repo.root / rel;
        try { if (fs::is_regular_file(fp)) fs::remove(fp); } catch (...) {}
    }

    // 5. Restore files from the target branch's commit tree
    std::string target = repo.get_branch_commit(branch);
    if (!target.empty()) {
        auto obj  = repo.load_object(target);
        auto data = CommitData::from_content(obj.content);
        if (!data.tree_hash.empty())
            repo.restore_tree(data.tree_hash, repo.root);
    }

    // 6. Clear staging area
    repo.save_index({});

    std::cout << "Switched to branch " << branch << "\n";
}
