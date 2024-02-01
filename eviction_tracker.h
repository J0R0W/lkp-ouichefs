#ifndef _EVICTION_TRACKER_H
#define _EVICTION_TRACKER_H

#include "eviction_policy.h"

// Change the eviction policy for a file system device
int eviction_tracker_change_policy(struct eviction_policy *eviction_policy);

struct eviction_tracker_scan_result {
	struct inode *best_candidate;
	struct inode *parent;
};

// Get the next inode to be evicted for the given device
bool eviction_tracker_get_inode_for_eviction(
	struct inode *dir, bool recurse,
	struct eviction_tracker_scan_result *result);
#endif