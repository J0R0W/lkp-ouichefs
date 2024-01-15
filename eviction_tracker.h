#ifndef _EVICTION_TRACKER_H
#define _EVICTION_TRACKER_H

#include <linux/rbtree.h>
#include "eviction_policy.h"

struct eviction_tracker {
	struct rb_root root;
	struct eviction_policy *eviction_policy;
	struct mutex lock;
	struct kref refcount;
};

struct eviction_tracker *
get_eviction_tracker(struct eviction_policy *eviction_policy);
void release_eviction_tracker(struct kref *refcount);
bool add_inode_to_eviction_tracker(struct eviction_tracker *eviction_tracker,
				   struct inode *inode, bool check_if_exists);
void update_inode_in_eviction_tracker(struct eviction_tracker *eviction_tracker,
				      struct inode *inode);
void remove_inode_from_eviction_tracker(
	struct eviction_tracker *eviction_tracker, struct inode *inode);
#endif