#ifndef _EVICTION_POLICY_H
#define _EVICTION_POLICY_H

#include <linux/fs.h>

// Interface for eviction policies
struct eviction_policy {
	// Function pointer that compares two inodes regarding eviction priority (higher value means higher priority)
	// Should return 1 if inode1 has higher priority, 0 if they have the same priority and -1 if inode2 has higher priority
	// The function pointer must remain valid as long as the eviction policy is registered (TODO: Find a better solution)
	int (*compare)(struct inode *inode1, struct inode *inode2);
};
#endif