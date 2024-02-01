#include <linux/buffer_head.h>

#include "ouichefs.h"
#include "eviction_tracker.h"
#include "eviction_policy_examples.h"

static struct eviction_policy *default_eviction_policy =
	&eviction_policy_least_recently_accessed;

// Is there a way to assign default_eviction_policy?
static struct eviction_policy *eviction_policy =
	&eviction_policy_least_recently_accessed;
static DEFINE_MUTEX(eviction_tracker_policy_mutex);

// TODO: Can we use a vfs layer function for this? This code shouldn't depend on the concrete file system implemenation
static void
_get_best_file_for_deletion(struct inode *dir, bool recurse,
			    struct eviction_policy *eviction_policy,
			    struct eviction_tracker_scan_result *result)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct ouichefs_file *f = NULL;
	int i;

	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return;
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for the file in directory */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		f = &dblock->files[i];
		if (!f->inode) {
			break;
		}

		struct inode *inode = ouichefs_iget(sb, f->inode);
		if (inode == NULL) {
			brelse(bh);
			return;
		}

		if (recurse && S_ISDIR(inode->i_mode)) {
			_get_best_file_for_deletion(inode, recurse,
						    eviction_policy, result);
		} else if (S_ISREG(inode->i_mode)) {
			if (result->best_candidate == NULL ||
			    eviction_policy->compare(
				    inode, result->best_candidate) > 0) {
				result->best_candidate = inode;
				result->parent = dir;
			}
		}

		iput(inode);
	}

	brelse(bh);
}

bool eviction_tracker_get_inode_for_eviction(
	struct inode *dir, bool recurse,
	struct eviction_tracker_scan_result *result)
{
	result->best_candidate = NULL;
	result->parent = NULL;

	mutex_lock(&eviction_tracker_policy_mutex);

	_get_best_file_for_deletion(dir, recurse, eviction_policy, result);
	if (result->best_candidate == NULL) {
		printk(KERN_INFO "no file found for eviction\n");
		mutex_unlock(&eviction_tracker_policy_mutex);
		return false;
	}

	mutex_unlock(&eviction_tracker_policy_mutex);

	return true;
}

int eviction_tracker_change_policy(struct eviction_policy *new_eviction_policy)
{
	mutex_lock(&eviction_tracker_policy_mutex);

	eviction_policy = new_eviction_policy ? new_eviction_policy :
						default_eviction_policy;

	mutex_unlock(&eviction_tracker_policy_mutex);
	return 0;
}
EXPORT_SYMBOL(eviction_tracker_change_policy);