#pragma once
/**
 * cmd_branch.h
 * ------------
 * `mini_vcs branch [name] [-d]` — list, create, or delete branches.
 *
 * Behaviour:
 *   (no args)      list all branches, marking the current one with '*'
 *   <name>         create a new branch pointing at the current commit
 *   <name> -d      delete the named branch ref file
 */

#include "repository.h"
#include <string>

/**
 * Execute the branch command.
 *
 * @param repo   open Repository
 * @param name   branch name (empty string → list mode)
 * @param del    true when -d flag was passed
 */
void cmd_branch(Repository& repo,
                const std::string& name,
                bool del);
