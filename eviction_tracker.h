#ifndef _EVICTION_TRACKER_H
#define _EVICTION_TRACKER_H

#include "eviction_policy.h"

/**
 * @brief Change the eviction policy used to compare inodes regarding their eviction priority
 * @param new_eviction_policy The new eviction policy to use, or NULL to reset to the default policy
 * @return 0 if successful
 */
int eviction_tracker_change_policy(struct eviction_policy *eviction_policy);

struct eviction_tracker_scan_result {
	struct inode *best_candidate;
	struct inode *parent;
};

/**
 * @brief Iterate over the directory and its subdirectories to find the best candidate for eviction. We are using inodes to represent files and directories and read the inode data block if it is a directory to get subfiles
 * @param dir Start directory
 * @param recurse Flag to indicate if we should recurse into subdirectories - for example true if we want eviction because of free space and false if we want eviction because of subfile limit in a directory
 * @param result Result parameter that will contain the best candidate for eviction and its parent directory
 * @return true if a candidate was found, false if no candidate was found
 */
bool eviction_tracker_get_inode_for_eviction(
	struct inode *dir, bool recurse,
	struct eviction_tracker_scan_result *result);
#endif
