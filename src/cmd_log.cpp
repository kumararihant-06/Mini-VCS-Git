/**
 * cmd_log.cpp
 * -----------
 * Implementation of `mini_vcs log`.
 */

#include "cmd_log.h"
#include "objects.h"

#include <ctime>
#include <iostream>
#include <string>

void cmd_log(const Repository& repo, int max_count) {
    std::string branch = repo.current_branch();
    std::string hash   = repo.get_branch_commit(branch);

    if (hash.empty()) {
        std::cout << "No commits yet!\n";
        return;
    }

    int count = 0;
    while (!hash.empty() && count < max_count) {
        auto obj  = repo.load_object(hash);
        auto data = CommitData::from_content(obj.content);

        std::cout << "commit " << hash << "\n";
        std::cout << "Author: " << data.author << "\n";
        time_t ts = (time_t)data.timestamp;
        std::cout << "Date:   " << std::string(ctime(&ts));   // ctime includes \n
        std::cout << "\n    " << data.message << "\n\n";

        hash = data.parent_hashes.empty() ? "" : data.parent_hashes[0];
        ++count;
    }
}
