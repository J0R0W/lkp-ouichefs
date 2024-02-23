# Eviction Process in ouiche_fs

## Overview
The `ouiche_fs` filesystem includes an eviction mechanism to handle scenarios where the filesystem is approaching its storage limits. Eviction, in the context of `ouiche_fs', refers to the process of selectively removing files or file metadata (inodes) from the filesystem to free up space and ensure that the filesystem can accommodate new files or grow existing files.

## Eviction Policy
We provided a mechanism for defining the eviction priority of inodes:
```C
struct eviction_policy {
	int (*compare)(struct inode *inode1, struct inode *inode2);
};
```
The compare function pointer takes 2 inodes and orders their eviction priority by returning an int:
1 if inode1 should be evicted before inode2
-1 if inode2 should be evicted before inode1
0 else


### Default Policy
The default policy is the **LRA** policy

### Available Example Policies
In `ouiche_fs`, various eviction policies can be implemented to determine the priority of files for eviction. Here are some of the example policies available (eviction_policy_examples.h):

- **Least Recently Accessed (LRA)**: The LRA policy evicts the files that have been least recently used.

- **Least Recently Modified (LRU)**: The LRU policy evicts the files that have been least recently modified.

- **Least Recently Created (LRC)**: The LRC policy evicts the files that have been least recently created.

- **Larest File Size (LFS)**: The LFS policy evicts the largest files first.

### Implementing your own policy
Including the eviction_tracker.h allows creating own policies and providing them to the eviction-mechanism.

An example module that does this resides in the subfolder eviction_policy_changer.
It is necessary to use the Module.symvers file that was used to build the `ouiche_fs` (see Makefile in the example project).

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

it accepts a directory in which subfiles are being compared regarding their eviction priority - it then returns (in the result parameter) the inode which is the top candidate.

This inode will then be unlinked from its parent directory.

### Automatic Eviction
Automatic eviction will trigger on the following occasions:
 - Creating files
 - Moving files (in a new directory)
 - Creating links

For moving files and creating specifically hardlinks only the non-recursive versions of eviction iteration will be called because it could only happen that the target dir is full but as no new space is required, a recursive search would be pointless.
In the case of creating symlinks or normal files, botth the non-recursive and the recursive search are being used depending on what is needed.

### Triggering Eviction

## Troubleshooting and Common Issues


## Conclusion

