# Eviction Process in ouiche_fs

## Overview
The `ouiche_fs` filesystem includes an eviction mechanism to handle scenarios where the filesystem is approaching its storage limits. Eviction, in the context of `ouiche_fs', refers to the process of selectively removing files or file metadata (inodes) from the filesystem to free up space and ensure that the filesystem can accommodate new files or grow existing files.

## Eviction Policy

### Default Policy

### Available Example Policies
In `ouiche_fs`, various eviction policies can be implemented to determine the priority of files or inodes for eviction. Here are some of the example policies available:

- **Least Recently Used (LRU)**: The LRU policy evicts the files or inodes that have been least recently used. It's based on the idea that files accessed or modified long ago are less likely to be needed shortly.

- **Least Frequently Used (LFU)**: The LFU policy prioritizes eviction based on the frequency of file access. Files or inodes that are accessed less frequently are considered first for eviction.

- **Largest File First (LFF)**: This policy targets the largest files in the filesystem for eviction, under the assumption that removing larger files will free up more space more quickly.

- **Least Recently Created (LRC)**: The LRC policy focuses on the creation time of files, opting to remove the oldest files from the filesystem first.

### Implementing your own policy
## Eviction Process

### Automatic Eviction

### Triggering Eviction

## Troubleshooting and Common Issues


## Conclusion

