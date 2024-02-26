/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */
#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "inode.h"
#include "ouichefs.h"
#include "bitmap.h"
#include "eviction_tracker.h"

static const struct inode_operations ouichefs_inode_ops;
static const struct inode_operations ouichefs_symlink_inode_ops;

/*
 * Get inode ino from disk.
 */
struct inode *ouichefs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode = NULL;
	struct ouichefs_inode *cinode = NULL;
	struct ouichefs_inode_info *ci = NULL;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL;
	uint32_t inode_block = (ino / OUICHEFS_INODES_PER_BLOCK) + 1;
	uint32_t inode_shift = ino % OUICHEFS_INODES_PER_BLOCK;
	int ret;

	/* Fail if ino is out of range */
	if (ino >= sbi->nr_inodes)
		return ERR_PTR(-EINVAL);

	/* Get a locked inode from Linux */
	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	/* If inode is in cache, return it */
	if (!(inode->i_state & I_NEW))
		return inode;

	ci = OUICHEFS_INODE(inode);
	/* Read inode from disk and initialize */
	bh = sb_bread(sb, inode_block);
	if (!bh) {
		ret = -EIO;
		goto failed;
	}
	cinode = (struct ouichefs_inode *)bh->b_data;
	cinode += inode_shift;

	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &ouichefs_inode_ops;

	inode->i_mode = le32_to_cpu(cinode->i_mode);
	i_uid_write(inode, le32_to_cpu(cinode->i_uid));
	i_gid_write(inode, le32_to_cpu(cinode->i_gid));
	inode->i_size = le32_to_cpu(cinode->i_size);
	inode->i_ctime.tv_sec = (time64_t)le32_to_cpu(cinode->i_ctime);
	inode->i_ctime.tv_nsec = 0;
	inode->i_atime.tv_sec = (time64_t)le32_to_cpu(cinode->i_atime);
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = (time64_t)le32_to_cpu(cinode->i_mtime);
	inode->i_mtime.tv_nsec = 0;
	inode->i_blocks = le32_to_cpu(cinode->i_blocks);
	set_nlink(inode, le32_to_cpu(cinode->i_nlink));

	ci->index_block = le32_to_cpu(cinode->index_block);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_fop = &ouichefs_dir_ops;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_fop = &ouichefs_file_ops;
		inode->i_op = &ouichefs_symlink_inode_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
	}

	brelse(bh);

	/* Unlock the inode to make it usable */
	unlock_new_inode(inode);

	return inode;

failed:
	brelse(bh);
	iget_failed(inode);
	return ERR_PTR(ret);
}

/*
 * Look for dentry in dir.
 * Fill dentry with NULL if not in dir, with the corresponding inode if found.
 * Returns NULL on success.
 */
static struct dentry *ouichefs_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct ouichefs_file *f = NULL;
	int i;

	/* Check filename length */
	if (dentry->d_name.len > OUICHEFS_FILENAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	/* Read the directory index block on disk */
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return ERR_PTR(-EIO);
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for the file in directory */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		f = &dblock->files[i];
		if (!f->inode)
			break;
		if (!strncmp(f->filename, dentry->d_name.name,
			     OUICHEFS_FILENAME_LEN)) {
			inode = ouichefs_iget(sb, f->inode);
			break;
		}
	}
	brelse(bh);

	/* Update directory access time */
	dir->i_atime = current_time(dir);
	mark_inode_dirty(dir);

	/* Fill the dentry with the inode */
	d_add(dentry, inode);

	return NULL;
}

/*
 * Create a new inode in dir.
 */
static struct inode *ouichefs_new_inode(struct inode *dir, mode_t mode)
{
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	uint32_t ino, bno;
	int ret;

	/* Check mode before doing anything to avoid undoing everything */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode)) {
		pr_err("File type not supported (only directory and regular files supported)\n");
		return ERR_PTR(-EINVAL);
	}

	/* Check if inodes are available */
	sb = dir->i_sb;
	sbi = OUICHEFS_SB(sb);

	if (sbi->nr_free_inodes == 0 || sbi->nr_free_blocks == 0)
		return ERR_PTR(-ENOSPC);

	/* Get a new free inode */
	ino = get_free_inode(sbi);
	if (!ino)
		return ERR_PTR(-ENOSPC);
	inode = ouichefs_iget(sb, ino);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto put_ino;
	}
	ci = OUICHEFS_INODE(inode);

	/* Get a free block for this new inode's index */
	bno = get_free_block(sb);
	if (!bno) {
		ret = -ENOSPC;
		goto put_inode;
	}
	ci->index_block = bno;

	/* Initialize inode */
	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_blocks = 1;
	if (S_ISDIR(mode)) {
		inode->i_size = OUICHEFS_BLOCK_SIZE;
		inode->i_fop = &ouichefs_dir_ops;
		set_nlink(inode, 2); /* . and .. */
	} else if (S_ISREG(mode)) {
		inode->i_size = 0;
		inode->i_fop = &ouichefs_file_ops;
		inode->i_mapping->a_ops = &ouichefs_aops;
		set_nlink(inode, 1);
	} else if (S_ISLNK(mode)) {
		inode->i_size = 0;
		inode->i_op = &ouichefs_symlink_inode_ops;
		set_nlink(inode, 1);
	}

	inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);

	return inode;

put_inode:
	iput(inode);
put_ino:
	put_inode(sbi, ino);

	return ERR_PTR(ret);
}

/*
 * Create a file or directory in this way:
 *   - check filename length and if the parent directory is not full
 *   - create the new inode (allocate inode and blocks)
 *   - cleanup index block of the new inode
 *   - add new file/directory in parent index
 */
static int ouichefs_create(struct mnt_idmap *idmap, struct inode *dir,
			   struct dentry *dentry, umode_t mode, bool excl)
{
	struct super_block *sb;
	struct inode *inode;
	struct ouichefs_inode_info *ci_dir;
	struct ouichefs_dir_block *dblock;
	char *fblock;
	struct buffer_head *bh, *bh2;
	int ret = 0, i;

	/* Check filename length */
	if (strlen(dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Read parent directory index */
	ci_dir = OUICHEFS_INODE(dir);
	sb = dir->i_sb;
	bh = sb_bread(sb, ci_dir->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;

	/* Check if parent directory is full */
	if (dblock->files[OUICHEFS_MAX_SUBFILES - 1].inode != 0) {
		struct eviction_tracker_scan_result result;

		if (!eviction_tracker_get_inode_for_eviction(dir, false,
							     &result)) {
			ret = -EMLINK;
			goto end;
		}

		pr_info("parent directory full - evicting inode %ld\n",
			result.best_candidate->i_ino);

		int ret_unlink = ouichefs_unlink_inode(result.parent,
						       result.best_candidate);
		if (ret_unlink < 0) {
			ret = ret_unlink;
			goto end;
		}
	}

	/* Get a new free inode */
	inode = ouichefs_new_inode(dir, mode);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto end;
	}

	/*
	 * Scrub index_block for new file/directory to avoid previous data
	 * messing with new file/directory.
	 */
	bh2 = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh2) {
		ret = -EIO;
		goto iput;
	}
	fblock = (char *)bh2->b_data;
	memset(fblock, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh2);
	brelse(bh2);

	/* Find first free slot in parent index and register new inode */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++)
		if (dblock->files[i].inode == 0)
			break;
	dblock->files[i].inode = inode->i_ino;
	strscpy(dblock->files[i].filename, dentry->d_name.name,
		OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update stats and mark dir and new inode dirty */
	mark_inode_dirty(inode);
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	if (S_ISDIR(mode))
		inode_inc_link_count(dir);
	mark_inode_dirty(dir);

	/* setup dentry */
	d_instantiate(dentry, inode);

	return 0;

iput:
	put_block(OUICHEFS_SB(sb), OUICHEFS_INODE(inode)->index_block);
	put_inode(OUICHEFS_SB(sb), inode->i_ino);
	iput(inode);
end:
	brelse(bh);
	return ret;
}

/*
 * Remove a link for a file. If link count is 0, destroy file in this way:
 *   - remove the file from its parent directory.
 *   - cleanup blocks containing data
 *   - cleanup file index block
 *   - cleanup inode
 */

int ouichefs_unlink_inode(struct inode *dir, struct inode *inode)
{
	struct super_block *sb = dir->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct buffer_head *bh = NULL, *bh2 = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	struct ouichefs_file_index_block *file_block = NULL;
	uint32_t ino, bno;
	int i, f_id = -1, nr_subs = 0;

	ino = inode->i_ino;
	bno = OUICHEFS_INODE(inode)->index_block;

	/* Read parent directory index */
	bh = sb_bread(sb, OUICHEFS_INODE(dir)->index_block);
	if (!bh)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh->b_data;

	/* Search for inode in parent index and get number of subfiles */
	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		/* This is problematic:
		 * We only check for inode ID not for the name
		 * If a directory contains 2 hardlinks to the same inode
		 * we will possibly remove the wrong one.
		 * We'd need some check to see if the name matches
		 * the dentry name
		 * but we don't have access to the dentry here
		 */
		if (dir_block->files[i].inode == ino)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;

	/* Remove file from parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id, dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1], 0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update inode stats */
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	if (S_ISDIR(inode->i_mode))
		inode_dec_link_count(dir);
	mark_inode_dirty(dir);

	/*
	 * Only decrement link count if link count is 2 or more (hardlinks)
	 * because later in this function
	 * the original decrement code will still run
	 */
	if (inode->i_nlink > 1) {
		inode_dec_link_count(inode);
		return 0;
	}

	/*
	 * Cleanup pointed blocks if unlinking a file. If we fail to read the
	 * index block, cleanup inode anyway and lose this file's blocks
	 * forever. If we fail to scrub a data block, don't fail (too late
	 * anyway), just put the block and continue.
	 */
	bh = sb_bread(sb, bno);
	if (!bh)
		goto clean_inode;
	file_block = (struct ouichefs_file_index_block *)bh->b_data;
	if (S_ISDIR(inode->i_mode))
		goto scrub;
	for (i = 0; i < inode->i_blocks - 1; i++) {
		char *block;

		if (!file_block->blocks[i])
			continue;

		put_block(sbi, file_block->blocks[i]);
		bh2 = sb_bread(sb, file_block->blocks[i]);
		if (!bh2)
			continue;
		block = (char *)bh2->b_data;
		memset(block, 0, OUICHEFS_BLOCK_SIZE);
		mark_buffer_dirty(bh2);
		brelse(bh2);
	}

scrub:
	/* Scrub index block */
	memset(file_block, 0, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh);
	brelse(bh);

clean_inode:
	/* Cleanup inode and mark dirty */
	inode->i_blocks = 0;
	OUICHEFS_INODE(inode)->index_block = 0;
	inode->i_size = 0;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	inode->i_mode = 0;
	inode->i_ctime.tv_sec = inode->i_mtime.tv_sec = inode->i_atime.tv_sec =
		0;
	inode_dec_link_count(inode);
	mark_inode_dirty(inode);

	/* Free inode and index block from bitmap */
	put_block(sbi, bno);
	put_inode(sbi, ino);

	return 0;
}

static int ouichefs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	return ouichefs_unlink_inode(dir, inode);
}

static int ouichefs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
			   struct dentry *old_dentry, struct inode *new_dir,
			   struct dentry *new_dentry, unsigned int flags)
{
	struct super_block *sb = old_dir->i_sb;
	struct ouichefs_inode_info *ci_old = OUICHEFS_INODE(old_dir);
	struct ouichefs_inode_info *ci_new = OUICHEFS_INODE(new_dir);
	struct inode *src = d_inode(old_dentry);
	struct buffer_head *bh_old = NULL, *bh_new = NULL;
	struct ouichefs_dir_block *dir_block = NULL;
	int i, f_id = -1, new_pos = -1, ret, nr_subs, f_pos = -1;

	/* fail with these unsupported flags */
	if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	/* Check if filename is not too long */
	if (strlen(new_dentry->d_name.name) > OUICHEFS_FILENAME_LEN)
		return -ENAMETOOLONG;

	/* Fail if new_dentry exists or if new_dir is full */
	bh_new = sb_bread(sb, ci_new->index_block);
	if (!bh_new)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_new->b_data;

	/* Check if new_dir is full (and != old_dir) and evict if necessary */
	if (new_dir != old_dir &&
	    dir_block->files[OUICHEFS_MAX_SUBFILES - 1].inode != 0) {
		struct eviction_tracker_scan_result result;

		if (!eviction_tracker_get_inode_for_eviction(new_dir, false,
							     &result)) {
			ret = -EMLINK;
			goto relse_new;
		}

		int ret_unlink = ouichefs_unlink_inode(result.parent,
						       result.best_candidate);
		if (ret_unlink < 0) {
			ret = ret_unlink;
			goto relse_new;
		}
	}

	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		/* if old_dir == new_dir, save the renamed file position */
		if (new_dir == old_dir) {
			if (strncmp(dir_block->files[i].filename,
				    old_dentry->d_name.name,
				    OUICHEFS_FILENAME_LEN) == 0)
				f_pos = i;
		}
		if (strncmp(dir_block->files[i].filename,
			    new_dentry->d_name.name,
			    OUICHEFS_FILENAME_LEN) == 0) {
			ret = -EEXIST;
			goto relse_new;
		}
		if (new_pos < 0 && dir_block->files[i].inode == 0)
			new_pos = i;
	}
	/* if old_dir == new_dir, just rename entry */
	if (old_dir == new_dir) {
		strscpy(dir_block->files[f_pos].filename,
			new_dentry->d_name.name, OUICHEFS_FILENAME_LEN);
		mark_buffer_dirty(bh_new);
		ret = 0;
		goto relse_new;
	}

	/* If new directory is empty, fail */
	if (new_pos < 0) {
		ret = -EMLINK;
		goto relse_new;
	}

	/* insert in new parent directory */
	dir_block->files[new_pos].inode = src->i_ino;
	strscpy(dir_block->files[new_pos].filename, new_dentry->d_name.name,
		OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh_new);
	brelse(bh_new);

	/* Update new parent inode metadata */
	new_dir->i_atime = new_dir->i_ctime = new_dir->i_mtime =
		current_time(new_dir);
	if (S_ISDIR(src->i_mode))
		inode_inc_link_count(new_dir);
	mark_inode_dirty(new_dir);

	/* remove target from old parent directory */
	bh_old = sb_bread(sb, ci_old->index_block);
	if (!bh_old)
		return -EIO;
	dir_block = (struct ouichefs_dir_block *)bh_old->b_data;
	/* Search for inode in old directory and number of subfiles */
	for (i = 0; OUICHEFS_MAX_SUBFILES; i++) {
		if (dir_block->files[i].inode == src->i_ino)
			f_id = i;
		else if (dir_block->files[i].inode == 0)
			break;
	}
	nr_subs = i;

	/* Remove file from old parent directory */
	if (f_id != OUICHEFS_MAX_SUBFILES - 1)
		memmove(dir_block->files + f_id, dir_block->files + f_id + 1,
			(nr_subs - f_id - 1) * sizeof(struct ouichefs_file));
	memset(&dir_block->files[nr_subs - 1], 0, sizeof(struct ouichefs_file));
	mark_buffer_dirty(bh_old);
	brelse(bh_old);

	/* Update old parent inode metadata */
	old_dir->i_atime = old_dir->i_ctime = old_dir->i_mtime =
		current_time(old_dir);
	if (S_ISDIR(src->i_mode))
		inode_dec_link_count(old_dir);
	mark_inode_dirty(old_dir);

	return 0;

relse_new:
	brelse(bh_new);
	return ret;
}

static int ouichefs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
			  struct dentry *dentry, umode_t mode)
{
	return ouichefs_create(NULL, dir, dentry, mode | S_IFDIR, 0);
}

static int ouichefs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh;
	struct ouichefs_dir_block *dblock;

	/* If the directory is not empty, fail */
	if (inode->i_nlink > 2)
		return -ENOTEMPTY;
	bh = sb_bread(sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh)
		return -EIO;
	dblock = (struct ouichefs_dir_block *)bh->b_data;
	if (dblock->files[0].inode != 0) {
		brelse(bh);
		return -ENOTEMPTY;
	}
	brelse(bh);

	/* Remove directory with unlink */
	return ouichefs_unlink(dir, dentry);
}

/**
 * @brief The function ouichefs_symlink creates a new symbolic link in the filesystem.
 * @param idmap  - Mapping information for the filesystem mount.
 * @param dir    - The inode of the parent directory in which the symlink is to be created.
 * @param dentry - The directory entry of the new symlink. This includes the name of the symlink.
 * @param symname - The target path the new symlink will point to. This is the pathname that the
 *                  new symlink refers to.
 * @return 0 on successful creation of the symlink. A negative error code is returned in case
 **/
static int ouichefs_symlink(struct mnt_idmap *idmap, struct inode *dir,
			    struct dentry *dentry, const char *symname)
{
	int ret = ouichefs_create(&nop_mnt_idmap, dir, dentry,
				  S_IFLNK | S_IRWXUGO, 0);
	if (ret < 0) {
		pr_err("symlink creation failed\n");
		return ret;
	}

	struct inode *inode = d_inode(dentry);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	struct buffer_head *bh =
		sb_bread(dir->i_sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh)
		return -EIO;

	/* Write the symname into the file block */
	strncpy(bh->b_data, symname, OUICHEFS_BLOCK_SIZE);
	mark_buffer_dirty(bh);
	brelse(bh);

	inode->i_size = strlen(symname);
	mark_inode_dirty(inode);

	return 0;
}

/* Callback after get_link is called so that bh can be released */
static void release_bh(void *arg)
{
	struct buffer_head *bh = (struct buffer_head *)arg;

	brelse(bh);
}

/**
 * @brief The function ouichefs_get_link is called to read the target path of a symbolic link.
 * @param dentry
 * @param inode The inode of the symlink.
 * @param done Callback to be called after the link is read.
 * @return The target path of the symlink.
 */
static const char *ouichefs_get_link(struct dentry *dentry, struct inode *inode,
				     struct delayed_call *done)
{
	struct buffer_head *bh =
		sb_bread(inode->i_sb, OUICHEFS_INODE(inode)->index_block);
	if (!bh)
		return ERR_PTR(-EIO);

	set_delayed_call(done, release_bh, bh);

	return bh->b_data;
}

/**
 * @brief The function ouichefs_link creates a new hard link in the filesystem.
 * @param old_dentry The directory entry of the existing file.
 * @param dir The inode of the parent directory in which the new hard link is to be created.
 * @param dentry The directory entry of the new hard link. This includes the name of the new hard link.
 * @return 0 on successful creation of the hard link. A negative error code is returned in case of failure.
 */
static int ouichefs_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *dentry)
{
	int ret = simple_link(old_dentry, dir, dentry);

	if (ret < 0) {
		pr_err("link creation failed\n");
		return ret;
	}

	struct inode *inode = d_inode(old_dentry);

	struct super_block *sb = dir->i_sb;
	struct ouichefs_inode_info *ci_dir = OUICHEFS_INODE(dir);
	struct buffer_head *bh = sb_bread(sb, ci_dir->index_block);

	if (!bh)
		return -EIO;

	struct ouichefs_dir_block *dblock =
		(struct ouichefs_dir_block *)bh->b_data;

	/* If target directory is full, evict an inode */
	if (dblock->files[OUICHEFS_MAX_SUBFILES - 1].inode != 0) {
		struct eviction_tracker_scan_result result;

		if (!eviction_tracker_get_inode_for_eviction(dir, false,
							     &result)) {
			dput(dentry);
			return -EMLINK;
		}

		int ret_unlink = ouichefs_unlink_inode(result.parent,
						       result.best_candidate);
		if (ret_unlink < 0) {
			dput(dentry);
			return ret_unlink;
		}
	}

	/* Find first free slot in parent index and register new inode */
	int i;

	for (i = 0; i < OUICHEFS_MAX_SUBFILES; i++) {
		if (dblock->files[i].inode == 0)
			break;
	}
	dblock->files[i].inode = inode->i_ino;
	strscpy(dblock->files[i].filename, dentry->d_name.name,
		OUICHEFS_FILENAME_LEN);
	mark_buffer_dirty(bh);
	brelse(bh);

	/* Update stats and mark dir and new inode dirty */
	dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
	mark_inode_dirty(dir);

	/*
	* I don't get why we must put the dentry here
	* but it seems to be necessary...
	*/
	dput(dentry);

	return 0;
}

static const struct inode_operations ouichefs_inode_ops = {
	.lookup = ouichefs_lookup,
	.create = ouichefs_create,
	.unlink = ouichefs_unlink,
	.mkdir = ouichefs_mkdir,
	.rmdir = ouichefs_rmdir,
	.rename = ouichefs_rename,
	.symlink = ouichefs_symlink,
	.link = ouichefs_link,
};

static const struct inode_operations ouichefs_symlink_inode_ops = {
	.get_link = ouichefs_get_link,
};
