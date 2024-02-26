#include <linux/buffer_head.h>

#include "ouichefs.h"
#include "eviction_tracker.h"
#include "eviction_policy_examples.h"
#include "dir.h"

static struct eviction_policy *default_eviction_policy =
	&eviction_policy_least_recently_accessed;

/* Is there a way to assign default_eviction_policy? */
static struct eviction_policy *eviction_policy =
	&eviction_policy_least_recently_accessed;
static DEFINE_MUTEX(eviction_tracker_policy_mutex);

/* Our extension of dir_context to provide additional fields */
struct eviction_tracker_iteration_context {
	struct dir_context ctx;
	bool recurse;
	struct super_block *sb;
	struct inode *parent;
	struct eviction_tracker_scan_result *result;
};

static bool eviction_tracker_iteration_actor(struct dir_context *ctx,
					     const char *name, int namelen,
					     loff_t offset, u64 ino,
					     unsigned d_type)
{
	struct eviction_tracker_iteration_context *eti_ctx = container_of(
		ctx, struct eviction_tracker_iteration_context, ctx);
	struct super_block *sb = eti_ctx->sb;

	struct inode *inode = ouichefs_iget(sb, ino);

	if (inode == NULL) {
		pr_err("inode not found\n");
		return false;
	}

	if (eti_ctx->recurse && S_ISDIR(inode->i_mode)) {
		/* Copy current context, just update parent and reset pos */
		struct eviction_tracker_iteration_context new_eti_ctx;

		memcpy(&new_eti_ctx, eti_ctx,
		       sizeof(struct eviction_tracker_iteration_context));
		new_eti_ctx.ctx.pos = 2;
		new_eti_ctx.parent = inode;

		/* Recurse into subdirectory */
		ouichefs_iterate_inode(inode, &new_eti_ctx.ctx);
	}

	else if ((S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) &&
		 atomic_read(&inode->i_readcount) == 0 &&
		 atomic_read(&inode->i_writecount) == 0) {
		if (eti_ctx->result->best_candidate == NULL ||
		    eviction_policy->compare(
			    inode, eti_ctx->result->best_candidate) > 0) {
			/*
			 * We found a better (or the first) candidate
			 * drop previous candidate (and parent) references
			 * and hold an additional reference to new candidate and parent
			 * (iput can handle NULL inodes)
			 */
			iput(eti_ctx->result->best_candidate);
			iput(eti_ctx->result->parent);
			ihold(inode);
			ihold(eti_ctx->parent);
			eti_ctx->result->best_candidate = inode;
			eti_ctx->result->parent = eti_ctx->parent;
		}
	}

	iput(inode);

	return true;
}

static void
_get_best_file_for_deletion_new(struct inode *dir, bool recurse,
				struct eviction_tracker_scan_result *result)
{
	struct eviction_tracker_iteration_context eti_ctx = {
		/* Set pos = 2 to skip . and .. */
		.ctx = { .actor = eviction_tracker_iteration_actor, .pos = 2 },
		.recurse = recurse,
		.result = result,
		.sb = dir->i_sb,
		.parent = dir,
	};

	ouichefs_iterate_inode(dir, &eti_ctx.ctx);
}

bool eviction_tracker_get_inode_for_eviction(
	struct inode *dir, bool recurse,
	struct eviction_tracker_scan_result *result)
{
	result->best_candidate = NULL;
	result->parent = NULL;

	mutex_lock(&eviction_tracker_policy_mutex);

	_get_best_file_for_deletion_new(dir, recurse, result);
	if (result->best_candidate == NULL) {
		pr_err("no file found for eviction\n");
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
