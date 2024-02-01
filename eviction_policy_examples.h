#ifndef _EVICTION_POLICY_EXAMPLES_H
#define _EVICTION_POLICY_EXAMPLES_H

#include "eviction_policy.h"

static int compare_timespec(struct timespec64 *timespec1,
			    struct timespec64 *timespec2)
{
	if (timespec1->tv_sec > timespec2->tv_sec)
		return -1;
	else if (timespec1->tv_sec < timespec2->tv_sec)
		return 1;
	else if (timespec1->tv_nsec > timespec2->tv_nsec)
		return -1;
	else if (timespec1->tv_nsec < timespec2->tv_nsec)
		return 1;
	else
		return 0;
}

static int compare_least_recently_accessed(struct inode *inode1,
					   struct inode *inode2)
{
	return compare_timespec(&inode1->i_atime, &inode2->i_atime);
}

struct eviction_policy eviction_policy_least_recently_accessed = {
	.compare = compare_least_recently_accessed,
};

static int compare_least_recenty_modified(struct inode *inode1,
					  struct inode *inode2)
{
	return compare_timespec(&inode1->i_mtime, &inode2->i_mtime);
}

struct eviction_policy eviction_policy_least_recently_modified = {
	.compare = compare_least_recenty_modified,
};

static int compare_least_recently_created(struct inode *inode1,
					  struct inode *inode2)
{
	return compare_timespec(&inode1->i_ctime, &inode2->i_ctime);
}

struct eviction_policy eviction_policy_least_recently_created = {
	.compare = compare_least_recently_created,
};

static int compare_largest_file(struct inode *inode1, struct inode *inode2)
{
	if (inode1->i_size > inode2->i_size)
		return 1;
	else if (inode1->i_size < inode2->i_size)
		return -1;
	else
		return 0;
}

struct eviction_policy eviction_policy_largest_file = {
	.compare = compare_largest_file,
};

#endif