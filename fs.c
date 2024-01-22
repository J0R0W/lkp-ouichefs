// SPDX-License-Identifier: GPL-2.0
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

#include "ouichefs.h"
#include "eviction_tracker.h"
/// @brief Trigger eviction for a device by writing its device id to the evict file
/// @param kobj
/// @param attr
/// @param buf
/// @param count
/// @return Number of bytes written
static ssize_t ouichefs_evict_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t count)
{
	dev_t device_id;
	int ret;

	ret = kstrtouint(buf, 10, &device_id);
	if (ret < 0) {
		printk(KERN_ERR "Invalid input: %s\n", buf);
		return ret;
	}

	// Perform the eviction
	struct inode *evicted_inode =
		eviction_tracker_get_inode_for_eviction(device_id);
	if (IS_ERR(evicted_inode)) {
		printk(KERN_ERR "Eviction failed for device %d\n", device_id);
		return PTR_ERR(evicted_inode);
	}

	printk(KERN_INFO "Eviction triggered for device %d\n", device_id);

	return count; // Return the number of bytes written
}

static ssize_t ouichefs_evict_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	// Optionally return information about eviction status

	dev_t devices[10];
	int count = eviction_tracker_get_registered_devices(devices, 10);
	printk(KERN_INFO "count: %d\n", count);
	for (int i = 0; i < count; i++) {
		struct inode *inode =
			eviction_tracker_get_inode_for_eviction(devices[i]);
		//print inode and devide id
		printk(KERN_INFO "device id: %d ", devices[i]);
		printk(KERN_INFO "inode: %lu\n", inode->i_ino);
	}

	return sprintf(buf, "%s\n", "Eviction Trigger Interface");
}

static struct kobj_attribute ouichefs_evict_attribute =
	__ATTR(evict, 0664, ouichefs_evict_show, ouichefs_evict_store);

static struct kobject *ouichefs_kobject;

//Todo Vielleicht funktion aus ouichefs.c verwenden
/*
* If a partition is mounted add all inodes to the eviction tracker. This function calls itself recursively
*/
int register_existing_inodes(struct inode *inode)
{
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct ouichefs_file *f = NULL;
	int i;

	/* Check that dir is a directory */
	if (!S_ISDIR(inode->i_mode)) {
		//add inode to the eviction tracker if the node is no directory
		eviction_tracker_add_inode(inode);
		return 0;
	}
	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Iterate over the index block and commit subfiles */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		f = &dblock->files[i];
		if (!f->inode)
			break;
		printk(KERN_INFO "file name: %s\n", f->filename);
		struct inode *inode = ouichefs_iget(sb, f->inode);
		//
		int x = register_existing_inodes(inode);

		iput(inode); //avoid Busy Node
		if (x < 0) {
			return x;
		}
	}

	brelse(bh);
	return 0;
}
#include "eviction_policy_examples.h"

/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;

	dentry =
		mount_bdev(fs_type, flags, dev_name, data, ouichefs_fill_super);
	if (IS_ERR(dentry)) {
		pr_err("'%s' mount failure\n", dev_name);
		return dentry;
	}
	pr_info("'%s' mount success\n", dev_name);
	int ret_evic = eviction_tracker_register_device(
		dentry->d_sb->s_dev, &eviction_policy_least_recently_accessed);
	if (ret_evic) {
		printk(KERN_INFO
		       "eviction tracker for device %d could not be registered\n",
		       dentry->d_sb->s_dev);
	}

	int dirs = register_existing_inodes(dentry->d_inode);
	printk(KERN_INFO "dirs_return: %d\n", dirs);

	printk(KERN_INFO "dentry name: %s\n", dentry->d_name.name);
	printk(KERN_INFO "device id: %d\n", dentry->d_sb->s_dev);
	//Get the superblock from inode
	struct super_block *sb = dentry->d_sb;
	//get sbi
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	printk(KERN_INFO "Total inodes: %u\n", sbi->nr_inodes);
	printk(KERN_INFO "Free inodes: %u\n", sbi->nr_free_inodes);
	return dentry;
}

/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	//kill_anon_super(sb);
	kill_block_super(sb);
	pr_info("unmounted disk\n");

	int ret_evic = eviction_tracker_unregister_device(sb->s_dev);
	if (ret_evic) {
		printk(KERN_INFO
		       "eviction tracker for device %d could not be unregistered\n",
		       sb->s_dev);
	}
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

	// Create a kobject and add it to the kernel
	ouichefs_kobject = kobject_create_and_add("ouichefs", kernel_kobj);
	if (!ouichefs_kobject) {
		pr_err("failed to create the ouichefs\n");
	}

	int error = sysfs_create_file(ouichefs_kobject,
				      &ouichefs_evict_attribute.attr);
	if (error) {
		pr_err("failed to create the foo file in /sys/kernel/ouichefs\n");
	}

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
