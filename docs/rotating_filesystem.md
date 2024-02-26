# Ouichefs as Rotating File System

## Overview
We extended ouichefs by adding a file-eviction-mechanism, effectively turning ouichefs into a rotating file system.

## Eviction Policy
We provided a mechanism for defining the eviction priority of inodes:
```C
struct eviction_policy {
	int (*compare)(struct inode *inode1, struct inode *inode2);
};
```
The compare function pointer takes 2 inodes and orders their eviction priority by returning an int:
* 1 if inode1 should be evicted before inode2
* -1 if inode2 should be evicted before inode1
* 0 else


### Default Policy
The default policy is the **LRA** policy

### Available Example Policies
In our ouichefs-extension, custom eviction policies can be implemented to determine the priority of files for eviction. Here are some of the example policies we already implemented (eviction_policy_examples.h):

- **Least Recently Accessed (LRA)**: The LRA policy evicts the files that have been least recently used.

- **Least Recently Modified (LRU)**: The LRU policy evicts the files that have been least recently modified.

- **Least Recently Created (LRC)**: The LRC policy evicts the files that have been least recently created.

- **Larest File Size (LFS)**: The LFS policy evicts the largest files first.

### Implementing your own policy
Including the eviction_tracker.h allows creating own policies and providing them to the eviction-mechanism.

An example module that does this resides in the subfolder eviction_policy_changer.
It is necessary to use the Module.symvers file that was used to build the ouichefs (see Makefile in the example project).

The interface for changing the eviction policy is:
```C
int eviction_tracker_change_policy(struct eviction_policy *eviction_policy);
```

## Eviction Process
The probably most interesting function is this:
```C
bool eviction_tracker_get_inode_for_eviction(
	struct inode *dir, bool recurse,
	struct eviction_tracker_scan_result *result);
```

it accepts a directory in which subfiles are being compared regarding their eviction priority - it then returns (in the result parameter) the inode which is the top candidate. Inodes in use (i_readcount != 0 or i_writecount != 0) will be ignored.

This inode will then be unlinked from its parent directory.

### Automatic Eviction
Automatic eviction (recursive, on filesystem root) will trigger whenever get_free_block is called and there are not enough blocks - this covers typical use cases as
 - Creating files
 - Writing to existing files
 - Creating symlinks

where the number of free blocks is relevant

Additionally automatic eviction (non-recursive, on relevant starting directory) will trigger when
- Files are created
- Files are moved into new directories
- Hardlinks are created

In these cases the eviction will only check if the number of subfiles are exceeding the limit.

### Triggering Eviction
We also provide a way to manually trigger evictions.
For non-recursive evictions:
```bash
echo -n "/path/to/start/directory" > /sys/kernel/ouichefs/evict
```

For recursive evictions:
```bash
echo -n "/path/to/start/directory" > /sys/kernel/ouichefs/evict_recursive
```

## Some Thoughts on the Eviction Process
Our first approach was to keep track of all files on the filesystem (ordered by their eviction priority - think of something like a binary tree of files).
While this approach seemed reasonable at first and guaranteeing low eviction latency, we later reconsidered that approach - mainly because of:
1. We had to keep pointers to all files/inodes in memory at all times - even though eviction is nowhere near to happen
	- although pointer only have a small memory footprint, we didn't consider this very nice
2. On file system mount we need to scan the whole file system for already existing files and add them to our datastructure
	- maybe using dcache could have helped with that issue - there we already have a (possibly incomplete) in-memory representation of the file hierarchy
3. For evictions triggered when a directory is full but there are still enough blocks, this suggested datastructe is of little help because we need to find a local optimum in that full directory and not a global optimum over the whole file system. This means for example that the already implemented RB-Tree in the Linux Kernel is not sophisticated enough anymore.
4. We have no knowledge of which fields a custom policy is using for comparison - thus when a file was opened for example, we can't know whether the order of eviction-priority needs to be re-evaluated
	- we could demand the policy on registration to provide fields that trigger a re-evaluation but this would repeat the DRY-principle. There probably is a solution to provide an interface which then also automatically recognizes which fields should trigger a re-evaluation (think of something like a macro READ_INODE_VALUE(field_name)) but this makes the interface ugly and (unnecessarily?) more complex
	- the result is we would need to re-evaluate our eviction priority ordering many times, possibly every time a file change occurs

Thus we took the given use case example of a "log filesystem" to heart and concluded that the uses cases probably look like this:
- frequent file accesses
- only small writes at a time
- small amount of files

From this we concluded that a very simple approach might actually fit the use cases best:
We simply scan all files (from a starting directory, recursive if necessary) whenever an eviction is necessary.
For the scanning we iterate over the inodes (and recursing into subdirectories if the inode happens to be a directory).

We reusing the iteration code in dir.c for an inode-based iteration. We don't really like this approach either because we rely on the file system implementation for this (references to dir.c) and we hope this file-iteration could be replaced by a VFS function. We noticed there is a iterate_dir function that seems to do what we need, but this function requires a struct file as parameter which seemed difficult to get from an inode as start-directory.

## Additioal Features
We implemented symlinks and hardlinks.
Hardlinks are currently bugged and if you create 2 hardlinks to the same inode in the same directory, deletion of the hardlinks might cause issues - this issue is documented in detail in the ouichefs_link function.

Both link types could be targets of evictions - in case of symlinks, only the actual symlink file will be evicted, leaving the target untouched. In case the target of a symlink gets evicted, all symlinks pointing to this target won't be touched. As symlinks from another file system out of ouichefs control, this seemed to be a reasonable and consistent approach.

In case a hardlink gets evicted, no other hardlinks to that inode will be touched, possibly resulting in no additional free space.
However in general our eviction process evicts until the eviction-condition is false, meaning it will just keep searching for the next best file to evict.

## Bugfixes
We found a small bug in the original filesystem.

It can be reproduced by running `df -i`. The IUsed output is wrong which is a result of a wrong assignment in super.c
```C
// Incorrect
stat->f_files = sbi->nr_inodes - sbi->nr_free_inodes;
// Correct
stat->f_files = sbi->nr_inodes;
```

## Current Bugs

### Hardlink Deletion
Hardlinks are currently bugged and if you create 2 hardlinks to the same inode in the same directory, deletion of the hardlinks might cause issues - this issue is documented in more detail in the ouichefs_link function.