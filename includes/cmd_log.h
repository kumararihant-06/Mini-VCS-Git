#pragma once
/**
 * cmd_log.h
 * ---------
 * `mini_vcs log [-n N]` — print commit history for the current branch.
 *
 * Walks the parent chain from HEAD, printing:
 *   commit <sha1>
 *   Author: <author>
 *   Date:   <human-readable date>
 *
 *       <message>
 */

#include "repository.h"

/**
 * Print up to `max_count` commits starting from the current branch tip.
 */
void cmd_log(const Repository& repo, int max_count = 10);
