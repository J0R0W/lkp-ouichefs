#include <linux/buffer_head.h>

#include "ouichefs.h"
#include "eviction_tracker.h"
#include "eviction_policy_examples.h"

static struct eviction_policy *eviction_policy;
static DEFINE_MUTEX(eviction_tracker_policy_mutex);

static struct inode *
_get_best_file_for_deletion(struct inode *dir, bool recurse,
			    struct eviction_policy *eviction_policy,
			    struct inode *best_candidate)
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
		return ERR_PTR(-EIO);
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
			return best_candidate;
		}

		if (recurse && S_ISDIR(inode->i_mode)) {
			best_candidate = _get_best_file_for_deletion(
				inode, recurse, eviction_policy,
				best_candidate);
		} else if (S_ISREG(inode->i_mode)) {
			if (best_candidate == NULL ||
			    eviction_policy->compare(inode, best_candidate) >
				    0) {
				best_candidate = inode;
			}
		} else {
			printk(KERN_INFO "inode %s is unknown\n", f->filename);
		}
		iput(inode);
	}

	brelse(bh);
	return best_candidate;
}

struct inode *eviction_tracker_get_inode_for_eviction(struct inode *dir,
						      bool recurse)
{
	mutex_lock(&eviction_tracker_policy_mutex);
	struct inode *best_candidate = _get_best_file_for_deletion(
		dir, recurse, eviction_policy, NULL);

	if (best_candidate == NULL) {
		printk(KERN_INFO "no file found for eviction\n");
		mutex_unlock(&eviction_tracker_policy_mutex);
		return ERR_PTR(-ENOENT);
	}

	mutex_unlock(&eviction_tracker_policy_mutex);
	return best_candidate;
}

int eviction_tracker_change_policy(struct eviction_policy *eviction_policy)
{
	mutex_lock(&eviction_tracker_policy_mutex);
	if (eviction_policy == NULL) {
		mutex_unlock(&eviction_tracker_policy_mutex);
		return -EINVAL;
	}
	eviction_policy = eviction_policy;
	mutex_unlock(&eviction_tracker_policy_mutex);

	return 0;
}