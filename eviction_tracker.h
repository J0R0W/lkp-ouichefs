#ifndef _EVICTION_TRACKER_H
#define _EVICTION_TRACKER_H

#include <linux/rbtree.h>
#include "eviction_policy.h"

// Register a file system device for eviction tracking
void eviction_tracker_register_device(dev_t device_id,
				      struct eviction_policy *eviction_policy);

// Unregister a file system device for eviction tracking
void eviction_tracker_unregister_device(dev_t device_id);

// Change the eviction policy for a file system device
void eviction_tracker_change_policy(dev_t device_id,
				    struct eviction_policy *eviction_policy);

// Add an inode to the eviction tracker
bool eviction_tracker_add_inode(struct inode *inode, bool check_if_exists);

// Remove an inode from the eviction tracker
void eviction_tracker_update_inode(struct inode *inode);

// Remove an inode from the eviction tracker
void eviction_tracker_remove_inode(struct inode *inode);

// Get the next inode to be evicted for the given device
struct inode *eviction_tracker_get_next_inode_for_eviction(dev_t device_id);
#endif