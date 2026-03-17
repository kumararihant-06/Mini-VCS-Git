#pragma once
/**
 * cmd_init.h
 * ----------
 * `mini_vcs init` — initialise a new repository.
 *
 * Creates the .git/ directory layout:
 *   .git/
 *     objects/
 *     refs/heads/
 *     HEAD              ← points to refs/heads/master
 *     index             ← empty staging area
 *     mini_vcs_config   ← chosen commit strategy (1/2/3)
 *
 * Interactively asks the user to choose a commit strategy if none is
 * provided via the optional `strategy` argument.
 */

#include "repository.h"
#include "commit_strategy.h"
#include <optional>

/**
 * Initialise repo at `repo.root`.
 *
 * @param repo      open Repository
 * @param strategy  pre-chosen strategy (nullopt = ask interactively)
 * @return false if .git/ already exists
 */
bool cmd_init(Repository& repo,
              std::optional<CommitStrategy> strategy = std::nullopt);
