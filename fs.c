/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/moduleparam.h>

#include "ouichefs.h"
#include "fs.h"
#include "inode.h"
#include "eviction_tracker.h"

MODULE_PARM_DESC(
	eviction_percentage_threshold,
	"Parameter how many blocks can be free before eviction is triggered (in %%) (Default: 10)");

/* Setter function used by struct kernel_parm_ops */
static int set_eviction_percentage_threshold(const char *val,
					     const struct kernel_param *kp)
{
	pr_info("Setting eviction_percentage_threshold to %s (currently: %d)\n",
		val, eviction_percentage_threshold);
	int ret = param_set_int(val, kp);

	if (ret < 0)
		return ret;

	if (eviction_percentage_threshold < 0 ||
	    eviction_percentage_threshold >= 100) {
		pr_err("Invalid eviction_percentage_threshold: %d - must be >= 0 and < 100\n",
		       eviction_percentage_threshold);
		return -EINVAL;
	}

	pr_info("eviction_percentage_threshold set to %d\n",
		eviction_percentage_threshold);
	return 0;
}

static const struct kernel_param_ops eviction_percentage_threshold_ops = {
	.get = param_get_int,
	.set = set_eviction_percentage_threshold,
};

module_param_cb(eviction_percentage_threshold,
		&eviction_percentage_threshold_ops,
		&eviction_percentage_threshold, 0664);

/* TODO: Check if the path is an ouichefs path
 * or belongs to another file system
 */
static ssize_t ouichefs_evict_store_general(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t count,
					    bool recurse)
{
	struct path path;
	int ret = kern_path(buf, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);

	if (ret < 0) {
		pr_err("Invalid input: %s\n", buf);
		return ret;
	}

	/* Get inode for eviction */
	struct eviction_tracker_scan_result result;

	if (!eviction_tracker_get_inode_for_eviction(d_inode(path.dentry),
						     recurse, &result)) {
		pr_err("Eviction failed for device %d and folder %s\n",
		       path.dentry->d_sb->s_dev, path.dentry->d_name.name);
		path_put(&path);
		return -ENOENT;
	}

	/* Trigger eviction for the target folder */
	ret = ouichefs_unlink_inode(result.parent, result.best_candidate);

	if (ret < 0) {
		pr_err("Eviction failed for device %d and folder %s\n",
		       path.dentry->d_sb->s_dev, path.dentry->d_name.name);
		path_put(&path);
		return ret;
	}

	/*
	 * Hacky bugfix: we use inodes to unlink files instead of dentries
	 * (mostly because we don't really understand
	 * how to properly use the dcache)
	 * so we need to prune leftover aliases in the dcache
	 * after unlinking the inode.
	 * If we used vfs_unlink() instead of ouichefs_unlink_inode() we
	 * probably wouldn't need to do this
	 * If we don't do this, something like this will fail:
	 * $ touch file1
	 * (trigger eviction of file1)
	 * $ touch file1
	 * --> file1 won't be created again because it's still in the dcache
	 *
	 * This hack is probably rather bad for performance...
	 *
	 * Our main issue is this: If we get an inode that we want to evict,
	 * we could call d_find_alias() to get a dentry for the inode
	 * 1. Is there only one alias for an inode? If we support hardlinks
	 * then probably no. How to make sure that we get the "corret" one?
	 * 2. Maybe that inode isn't currently present in the dcache? We
	 * would like a function that forces the dcache to load the inode
	 * into the cache so we can get the dentry for it.
	*/
	d_prune_aliases(result.best_candidate);

	iput(result.best_candidate);
	iput(result.parent);

	path_put(&path);
	return count;
}

/*
 * Trigger recursive eviction for a directory
 * by writing the start path to the evict_recursive file
 */
static ssize_t ouichefs_evict_recursive_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	return ouichefs_evict_store_general(kobj, attr, buf, count, true);
}

/*
 * Trigger non-recursive eviction for a directory
 * by writing the start path to the evict file
 */
static ssize_t ouichefs_evict_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	return ouichefs_evict_store_general(kobj, attr, buf, count, false);
}

static struct kobj_attribute ouichefs_evict_recursive_attribute =
	__ATTR(evict_recursive, 0664, NULL, ouichefs_evict_recursive_store);

static struct kobj_attribute ouichefs_evict_attribute =
	__ATTR(evict, 0664, NULL, ouichefs_evict_store);

static struct kobject *ouichefs_kobject;

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;

	dentry =
		mount_bdev(fs_type, flags, dev_name, data, ouichefs_fill_super);
	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else
		pr_info("'%s' mount success\n", dev_name);

	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	kill_block_super(sb);

	pr_info("unmounted disk\n");
}

static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};

static int __init ouichefs_init(void)
{
	int ret;

	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto err;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto err_inode;
	}

	/* Create a kobject and add it to the kernel */
	ouichefs_kobject = kobject_create_and_add("ouichefs", kernel_kobj);

	if (!ouichefs_kobject)
		pr_err("failed to create the ouichefs\n");

	int error = sysfs_create_file(ouichefs_kobject,
				      &ouichefs_evict_recursive_attribute.attr);
	if (error)
		pr_err("failed to create the recursive file in /sys/kernel/ouichefs\n");

	error = sysfs_create_file(ouichefs_kobject,
				  &ouichefs_evict_attribute.attr);

	if (error)
		pr_err("failed to create the non-recursive file in /sys/kernel/ouichefs\n");

	pr_info("module loaded\n");
	return 0;

err_inode:
	ouichefs_destroy_inode_cache();
err:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	ouichefs_destroy_inode_cache();
	kobject_put(ouichefs_kobject);
	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@rwth-aachen.de>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");
