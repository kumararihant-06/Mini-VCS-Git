/**
 * cmd_add.cpp
 * -----------
 * Implementation of `mini_vcs add`.
 */

#include "cmd_add.h"
#include "objects.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void cmd_add_file(Repository& repo, const std::string& rel) {
    fs::path full = repo.root / rel;
    if (!fs::exists(full))
        throw std::runtime_error("Path not found: " + rel);

    Blob        blob(read_file(full));
    std::string h = blob.hash();

    auto idx = repo.load_index();
    idx[rel] = h;
    repo.save_index(idx);

    std::cout << "Added " << rel << "\n";
}

void cmd_add_directory(Repository& repo, const std::string& rel) {
    fs::path full = repo.root / rel;
    if (!fs::exists(full) || !fs::is_directory(full))
        throw std::runtime_error("Not a directory: " + rel);

    auto idx   = repo.load_index();
    int  count = 0;

    for (const auto& entry : fs::recursive_directory_iterator(full)) {
        if (!entry.is_regular_file()) continue;

        const fs::path& p = entry.path();

        // Skip anything inside .git/
        bool in_git = false;
        for (const auto& part : p)
            if (part == ".git") { in_git = true; break; }
        if (in_git) continue;

        std::string file_rel = fs::relative(p, repo.root).generic_string();
        Blob blob(read_file(p));
        idx[file_rel] = blob.hash();
        ++count;
    }

    repo.save_index(idx);

    if (count)
        std::cout << "Added " << count << " files from directory " << rel << "\n";
    else
        std::cout << "Directory " << rel << " already up to date\n";
}

void cmd_add(Repository& repo, const std::string& rel) {
    fs::path full = repo.root / rel;
    if (!fs::exists(full))
        throw std::runtime_error("Path not found: " + rel);

    if (fs::is_regular_file(full))
        cmd_add_file(repo, rel);
    else if (fs::is_directory(full))
        cmd_add_directory(repo, rel);
    else
        throw std::runtime_error("Not a file or directory: " + rel);
}
